/**
 * The Phase 1 gate driver (docs/sb-platform-port.md): a scripted two-client Hyperion match.
 *
 * Proves the ladder port's server half end to end, without the Electron app, the injected game
 * DLL, or a relay: two Hyperion-engine clients (plain HTTP + WebSocket, `Origin: hyperion://…`)
 * log in, queue, accept a match, report their game loops started, submit reconciled results, and
 * the server applies Glicko-2 rating and ladder-point changes for both.
 *
 * Expected environment (brought up by `gate.sh`): Postgres and Redis from the vendored
 * `docker-compose.yml`, migrations applied, server-rs on `SB_GQL_PORT`, the Node server on
 * `SB_HTTP_PORT`, and the dev accounts seeded (`pnpm run seed-dev`). Re-runnable: seeding is
 * idempotent, and fresh matchmaking seasons/times/map-pool rows are inserted each run.
 */
import { createHash } from 'node:crypto'
import { readdir, readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import got from 'got'
import pg from 'pg'
import createNydus from 'nydus-client'

const HERE = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(HERE, '..', '..')
const BASE = (process.env.SB_CANONICAL_HOST ?? 'http://localhost:5555').replace(/\/$/, '')
const WS_URL = BASE.replace(/^http/, 'ws')
const DATABASE_URL = process.env.DATABASE_URL
const SEED_PASSWORD = 'shieldbattery'

if (!DATABASE_URL) {
  console.error('gate: DATABASE_URL is not set; run gate.sh (it copies .env from sample.env)')
  process.exit(1)
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))

const die = msg => {
  console.error(`gate: FAIL — ${msg}`)
  process.exit(1)
}

const log = (...args) => console.log('gate:', ...args)

/**
 * Applies the vendored migrations with the same bookkeeping sqlx uses (`_sqlx_migrations`,
 * SHA-384 of the file bytes), so a later `cargo sqlx migrate run` accepts what this runner did.
 */
async function migrate(db) {
  await db.query(`
    CREATE TABLE IF NOT EXISTS _sqlx_migrations (
      version BIGINT PRIMARY KEY,
      description TEXT NOT NULL,
      installed_on TIMESTAMPTZ NOT NULL DEFAULT now(),
      success BOOLEAN NOT NULL,
      checksum BYTEA NOT NULL,
      execution_time BIGINT NOT NULL
    )`)
  const { rows } = await db.query('SELECT version FROM _sqlx_migrations')
  const applied = new Set(rows.map(r => String(r.version)))
  const files = (await readdir(path.join(ROOT, 'migrations')))
    .filter(f => f.endsWith('.sql'))
    .sort()
  for (const file of files) {
    const version = file.split('_')[0]
    if (applied.has(version)) continue
    const body = await readFile(path.join(ROOT, 'migrations', file), 'utf8')
    const checksum = createHash('sha384').update(body).digest()
    const started = Date.now()
    await db.query('BEGIN')
    try {
      await db.query(body)
      await db.query(
        `INSERT INTO _sqlx_migrations (version, description, success, checksum, execution_time)
         VALUES ($1, $2, true, $3, $4)`,
        [version, file.slice(file.indexOf('_') + 1, -4), checksum, Date.now() - started],
      )
      await db.query('COMMIT')
    } catch (err) {
      await db.query('ROLLBACK')
      throw err
    }
  }
}

/** Seeds a matchmaking season, an enabled 1v1 time window, a map and a map pool for the run. */
async function seedMatchmaking(db) {
  const users = await db.query(`SELECT id FROM users WHERE name = 'claude-admin'`)
  if (!users.rows.length) {
    die("seeded dev account 'claude-admin' not found; run `pnpm run seed-dev` first")
  }
  const adminId = users.rows[0].id

  const start = new Date(Date.now() - 24 * 60 * 60 * 1000)
  await db.query(`INSERT INTO matchmaking_seasons (start_date, name, reset_mmr) VALUES ($1, $2, false)`,
    [start, 'hyperion gate season'])
  await db.query(`INSERT INTO matchmaking_times (matchmaking_type, start_date, enabled) VALUES ('1v1', $1, true)`,
    [start])

  const mapHash = Buffer.from('0123456789abcdef'.repeat(4), 'hex')
  await db.query(`
    INSERT INTO maps (hash, extension, title, description, width, height, tileset,
      players_melee, players_ums, lobby_init_data, is_eud, parser_version, image_version)
    VALUES ($1, 'scm', 'Hyperion Gate Map', '', 128, 128, 'badlands', 2, 2, '{}', false, 1, 1)
    ON CONFLICT (hash) DO NOTHING`, [mapHash])
  const uploaded = await db.query(`
    INSERT INTO uploaded_maps (id, map_hash, uploaded_by, upload_date, visibility, name, description)
    VALUES (sb_uuid(), $1, $2, CURRENT_TIMESTAMP AT TIME ZONE 'UTC', 'PUBLIC', 'Hyperion Gate Map', '')
    RETURNING id`, [mapHash, adminId])
  const mapId = uploaded.rows[0].id

  await db.query(`
    INSERT INTO matchmaking_map_pools (matchmaking_type, start_date, maps, max_veto_count)
    VALUES ('1v1', $1, ARRAY[$2]::uuid[], 0)`, [start, mapId])

  log('seeded season, 1v1 window, map and map pool')
}

async function login(username) {
  const res = await got.post(`${BASE}/api/1/sessions`, {
    json: { username, password: SEED_PASSWORD, remember: true, clientIds: [] },
    responseType: 'json',
    headers: { Origin: 'hyperion://gate' },
  })
  const body = res.body
  return { userId: body.user.id, jwt: body.jwt }
}

function connectSocket(account) {
  return new Promise((resolve, reject) => {
    const clientId = `gate-${account.userId}`
    const socket = createNydus(WS_URL, {
      query: { clientId },
      extraHeaders: { Authorization: `Bearer ${account.jwt}`, Origin: 'hyperion://gate' },
      connectTimeout: 10000,
    })
    const timer = setTimeout(() => reject(new Error(`socket connect timed out (${account.userId})`)), 15000)
    socket.on('connect', () => {
      clearTimeout(timer)
      resolve(socket)
    })
    socket.on('connectError', err => {
      clearTimeout(timer)
      reject(err)
    })
  })
}

/** Registers the routes a Hyperion client listens on and returns a per-user event tap. */
function tapEvents(socket) {
  const events = []
  for (const pattern of [
    '/matchmaking/:userId',
    '/matchmaking/:userId/:clientId',
    '/matchmaking/matches/:matchId',
    '/gameLoader/:gameId/:userId',
  ]) {
    socket.registerRoute(pattern, (route, event) => {
      events.push({ route: route.route, params: route.params, event })
    })
  }
  return events
}

const waitFor = async (label, check, timeoutMs) => {
  const deadline = Date.now() + timeoutMs
  for (;;) {
    if (await check()) return
    if (Date.now() > deadline) die(`timed out waiting for: ${label}`)
    await sleep(250)
  }
}

async function main() {
  const db = new pg.Client({ connectionString: DATABASE_URL })
  for (let attempt = 0; ; attempt++) {
    try {
      await db.connect()
      break
    } catch {
      if (attempt > 30) die('Postgres never became ready')
      await sleep(1000)
    }
  }

  await migrate(db)
  await seedMatchmaking(db)

  log('logging in the two clients')
  const a = await login('claude-1')
  const b = await login('claude-2')
  const accounts = [a, b]

  log('connecting both clients over WebSocket (Origin: hyperion://)')
  const sockets = await Promise.all(accounts.map(connectSocket))
  const events = sockets.map(tapEvents)

  log('queueing both clients for 1v1')
  for (const [i, account] of accounts.entries()) {
    await got.post(`${BASE}/api/1/matchmaking/find`, {
      json: {
        clientId: `gate-${account.userId}`,
        preferences: [
          { userId: account.userId, matchmakingType: '1v1', race: 't', mapSelections: [] },
        ],
        identifiers: [],
      },
      headers: { Authorization: `Bearer ${account.jwt}` },
    })
  }

  await waitFor(
    'matchFound events',
    () => events.filter(e => e.event?.type === 'matchFound').length >= 2,
    30000,
  )
  log('match found; accepting')
  for (const account of accounts) {
    await got.post(`${BASE}/api/1/matchmaking/accept`, {
      headers: { Authorization: `Bearer ${account.jwt}` },
    })
  }

  await waitFor(
    'setGameConfig events',
    () => events.filter(e => e.event?.type === 'setGameConfig').length >= 2,
    30000,
  )
  const setups = events
    .filter(e => e.event?.type === 'setGameConfig')
    .map(e => ({ gameId: e.event.gameId, setup: e.event.setup }))
  if (new Set(setups.map(s => s.gameId)).size !== 1) {
    die(`both clients must receive the same game; got ${JSON.stringify(setups.map(s => s.gameId))}`)
  }
  const { gameId } = setups[0]
  if (setups.some(s => s.setup.useNetcodeV2)) {
    die('a Hyperion game must never mint a netcode v2 session')
  }
  log(`game ${gameId} configured; reporting both game loops started`)

  for (const account of accounts) {
    await got.put(`${BASE}/api/1/games/${gameId}/status`, {
      json: { status: 5 /* GameStatus.Playing */ },
      headers: { Authorization: `Bearer ${account.jwt}` },
    })
  }

  await waitFor(
    'gameStarted events',
    () => events.filter(e => e.event?.type === 'gameStarted').length >= 1,
    15000,
  )
  log('game started; submitting reconciled results (claude-1 wins)')

  const [winner, loser] = accounts
  const report = (userId, resultCode) => ({
    userId,
    resultCode,
    time: Date.now(),
    playerResults: [
      [winner.userId, { result: 3 /* Victory */, race: 't', apm: 0 }],
      [loser.userId, { result: 2 /* Defeat */, race: 'z', apm: 0 }],
    ],
  })
  // Each client submits with its own minted resultCode. The setup doesn't carry the user id, so
  // map setups back to users through the gameLoader route params captured at delivery.
  const setupByUser = new Map()
  for (const delivery of events.filter(e => e.event?.type === 'setGameConfig' && e.params?.userId)) {
    setupByUser.set(Number(delivery.params.userId), delivery.event.setup)
  }
  if (setupByUser.size !== 2) {
    die('could not map setGameConfig setups to users')
  }
  for (const account of accounts) {
    const setup = setupByUser.get(account.userId)
    await got.post(`${BASE}/api/1/games/${gameId}/results`, {
      json: report(account.userId, setup.resultCode),
    })
  }
  log('both results submitted; waiting for reconciliation and rating changes')

  await waitFor(
    'reconciled rating changes',
    async () => {
      const res = await db.query(
        `SELECT COUNT(*)::int AS n FROM matchmaking_rating_changes WHERE game_id = $1`,
        [gameId],
      )
      return res.rows[0].n === 2
    },
    30000,
  )

  const ratings = await db.query(
    `SELECT user_id, rating, uncertainty, points, bonus_used, lifetime_games
     FROM matchmaking_ratings
     WHERE user_id = ANY($1) AND matchmaking_type = '1v1'`,
    [[winner.userId, loser.userId]],
  )
  if (ratings.rows.length !== 2) die('expected one rating row per player')
  const changes = await db.query(
    `SELECT user_id, outcome, rating_change, points_change
     FROM matchmaking_rating_changes
     WHERE game_id = $1 ORDER BY user_id`,
    [gameId],
  )

  const byUser = new Map(ratings.rows.map(r => [r.user_id, r]))
  const winnerRow = byUser.get(winner.userId)
  const loserRow = byUser.get(loser.userId)
  if (!(winnerRow.rating > loserRow.rating)) {
    die(`winner should rate above the loser; got ${winnerRow.rating} vs ${loserRow.rating}`)
  }
  if (!(winnerRow.points > loserRow.points)) {
    die(`winner should have more points; got ${winnerRow.points} vs ${loserRow.points}`)
  }

  console.log('')
  console.log('gate: PASS — a full Hyperion match ran through the platform:')
  for (const c of changes.rows) {
    console.log(
      `  user ${c.user_id}: ${c.outcome}, rating ${c.rating_change >= 0 ? '+' : ''}${c.rating_change.toFixed(2)}, points ${c.points_change >= 0 ? '+' : ''}${c.points_change.toFixed(2)}`,
    )
  }
  console.log(`  final ratings: claude-1 ${winnerRow.rating.toFixed(2)} (${winnerRow.points.toFixed(1)} pts), claude-2 ${loserRow.rating.toFixed(2)} (${loserRow.points.toFixed(1)} pts)`)

  for (const socket of sockets) socket.disconnect()
  await db.end()
}

main().catch(err => {
  console.error(err)
  process.exit(1)
})
