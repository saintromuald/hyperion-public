import Koa from 'koa'

export function isElectronClient(ctx: Koa.Context) {
  const origin = ctx.get('Origin') || ''
  return origin.startsWith('shieldbattery://')
}

/**
 * Whether the connection comes from the Hyperion engine, which speaks the platform's APIs directly
 * instead of going through the Electron app. The origin is the only discriminator available at the
 * socket and session layers; it is a transport hint, not an authentication or attestation boundary.
 */
export function isHyperionClient(ctx: Koa.Context) {
  const origin = ctx.get('Origin') || ''
  return origin.startsWith('hyperion://')
}
