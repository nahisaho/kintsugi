#!/usr/bin/env bash
# Builds and runs the full C++ test suite. Used as the general "commands"
# gate check (musubix3 gate / evidence refresh), independent of the
# single-test-scoped TDD wrapper (scripts/run_tdd_test.sh). Also emits a
# musubix-json "testReport" (.musubix/reports/tdd/cpp-tests.json) listing
# every doctest TEST-* case's pass/fail status, so musubix3's
# model-correspondence check has authoritative full-suite evidence to link
# formally-modeled requirements to their passing tests.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
REPORT_DIR="$ROOT_DIR/.musubix/reports/tdd"
XML_PATH="$REPORT_DIR/cpp-tests.xml"
REPORT_PATH="$REPORT_DIR/cpp-tests.json"

mkdir -p "$REPORT_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" --target pottery_tests -j"$(nproc)"

set +e
"$BUILD_DIR/pottery_tests"
EXIT_CODE=$?
"$BUILD_DIR/pottery_tests" --reporters=xml --out="$XML_PATH" > /dev/null 2>&1
set -e

python3 - "$XML_PATH" "$REPORT_PATH" <<'PYEOF'
import json
import re
import sys
import xml.etree.ElementTree as ET

xml_path, report_path = sys.argv[1], sys.argv[2]
root = ET.parse(xml_path).getroot()
tests = []
id_pattern = re.compile(r'^(TEST-[A-Za-z0-9-]+)')
for case in root.iter('TestCase'):
    match = id_pattern.match(case.attrib.get('name', ''))
    if not match:
        continue
    overall = case.find('OverallResultsAsserts')
    passed = overall is not None and overall.attrib.get('test_case_success') == 'true'
    tests.append({'id': match.group(1), 'status': 'passed' if passed else 'failed'})

with open(report_path, 'w', encoding='utf-8') as f:
    json.dump({'schemaVersion': 1, 'tests': tests}, f)
PYEOF

exit "$EXIT_CODE"
