"""Run deterministic native tests; no Pluto match or inference is ever launched.

    python3 engine/tools/pluto-native-tests/run_tests.py
    python3 engine/tools/pluto-native-tests/run_tests.py --engine /path/Hyperion.x86_64 \
        --data /path/Data --install-folder '/path/to/licensed/StarCraft' \
        --output /tmp/opencode/native-results --only 'larva*'

Without --engine, command/fixture tests run and engine scenarios are reported as
skipped.  Supplying a bad engine is an error, not another reason to skip.
"""
import argparse
import fnmatch
import os
from pathlib import Path
import sys
import time
import unittest


def filtered_suite(suite, patterns):
    if not patterns:
        return suite
    selected = unittest.TestSuite()
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            child = filtered_suite(test, patterns)
            if child.countTestCases():
                selected.addTests(child)
        elif any(fnmatch.fnmatchcase(test._testMethodName, pattern) for pattern in patterns):
            selected.addTest(test)
    return selected


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", help="explicit Hyperion executable containing the Phase 2 turn command")
    parser.add_argument("--data", help="Hyperion data folder (default: the engine's folder)")
    parser.add_argument("--install-folder", help="licensed StarCraft data install used by the engine")
    parser.add_argument("--output", help="new parent for per-scenario HXM, JSONL, logs and mismatch reports")
    parser.add_argument("--only", action="append", default=[],
                        help="fnmatch pattern for scenario method names; repeatable (for example 'larva*')")
    parser.add_argument("--pure-only", action="store_true", help="omit engine scenarios even with --engine")
    args = parser.parse_args(argv)
    if args.pure_only and args.engine:
        parser.error("--pure-only and --engine are mutually exclusive")
    if args.output and not args.engine:
        parser.error("--output is meaningful only with --engine")

    if args.engine:
        engine = Path(args.engine).expanduser().resolve()
        output = Path(args.output).expanduser().resolve() if args.output else Path(
            "/tmp/opencode" if os.name != "nt" else os.environ.get("TEMP", ".")) / (
                f"pluto-native-tests-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}")
        os.environ["HYPERION_NATIVE_ENGINE"] = str(engine)
        os.environ["HYPERION_NATIVE_OUTPUT"] = str(output)
        if args.install_folder:
            os.environ["HYPERION_NATIVE_INSTALL"] = str(Path(args.install_folder).expanduser().resolve())
        else:
            os.environ.pop("HYPERION_NATIVE_INSTALL", None)
        if args.data:
            os.environ["HYPERION_NATIVE_DATA"] = str(Path(args.data).expanduser().resolve())
        else:
            os.environ.pop("HYPERION_NATIVE_DATA", None)
    else:
        output = None
        os.environ.pop("HYPERION_NATIVE_ENGINE", None)
        os.environ.pop("HYPERION_NATIVE_DATA", None)
        os.environ.pop("HYPERION_NATIVE_OUTPUT", None)
        os.environ.pop("HYPERION_NATIVE_INSTALL", None)

    # Import only after the explicit engine configuration is in place: the test
    # class's skip decision and process runner read these values at import time.
    import test_native  # noqa: E402

    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(test_native)
    if args.pure_only:
        suite = unittest.TestSuite()
        suite.addTests(loader.loadTestsFromTestCase(test_native.CommandPackingTests))
        suite.addTests(loader.loadTestsFromTestCase(test_native.FixtureValidityTests))
        suite.addTests(loader.loadTestsFromTestCase(test_native.PortRegressionTests))
    suite = filtered_suite(suite, args.only)
    if not suite.countTestCases():
        parser.error("no tests match --only")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if output is not None:
        print(f"native engine evidence: {output}")
    print(f"tests={result.testsRun} failures={len(result.failures)} errors={len(result.errors)} skipped={len(result.skipped)}")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
