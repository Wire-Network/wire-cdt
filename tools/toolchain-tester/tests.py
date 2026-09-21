from __future__ import annotations
from typing import TYPE_CHECKING
from typing import Dict, List
from abc import ABC, abstractmethod

import difflib
import json
import os
import subprocess
import re

from printer import Printer as P
from errors import TestFailure

if TYPE_CHECKING:
    from testsuite import TestSuite


class Test(ABC):
    """
    This class represents a singular test case: one entry of a test file's JSON.

    Every case compiles inside its own working directory,
    ``<work root>/<suite>/<case>`` (``work_dir``), so the objects, ``.desc``
    descriptors, ``.abi`` and ``.wasm`` it produces can never be read or
    overwritten by another case running at the same time. The toolchain names
    those artifacts after the SOURCE basename, so without this isolation two
    cases that compile an ``other.cpp`` -- or the three cases of one JSON file,
    which all compile the same ``.cpp`` -- race each other in a shared directory
    and fail nondeterministically under parallel execution.

    The directory is kept after the run so a failing case can be inspected; its
    path is printed with the failure.
    """

    def __init__(
        self, cpp_file: str, test_json: Dict, index: int, test_suite: TestSuite
    ):
        self.cpp_file: str = cpp_file
        self.test_json: Dict = test_json
        self.test_suite: TestSuite = test_suite

        self._name = cpp_file.split("/")[-1].split(".")[0]
        self.name: str = f"{self._name}_{index}"

        self.fullname: str = f"{test_suite.name}/{self.name}"

        self.work_dir: str = os.path.join(
            test_suite.work_root, test_suite.name, self.name
        )

        self.out_abi: str = os.path.join(self.work_dir, f"{self._name}.abi")
        self.out_wasm: str = os.path.join(self.work_dir, f"{self._name}.wasm")

        self.success: bool = False

    @abstractmethod
    def _run(self, cdt_cpp: str, args: List[str]):
        pass

    def run(self):
        cf = self.test_json.get("compile_flags")
        args = cf if cf else []
        args = [arg.replace("{cwd}", self.test_suite.directory) for arg in args]

        cdt_cpp = os.path.join(self.test_suite.cdt_path, "cdt-cpp")

        os.makedirs(self.work_dir, exist_ok=True)
        self._run(cdt_cpp, args)

    def _run_cdt_cpp(
        self,
        cdt_cpp: str,
        leading_args: List[str],
        args: List[str],
        expected_pass: bool = True,
    ) -> subprocess.CompletedProcess:
        """
        Invoke ``cdt-cpp`` on this case's source inside ``work_dir`` and check
        the result.

        :param cdt_cpp: path to the ``cdt-cpp`` driver
        :param leading_args: driver flags that define the test kind, placed
            before the source file (``-c`` for compile-only,
            ``-abigen_output=''`` for ABI generation)
        :param args: the case's ``compile_flags`` from its JSON
        :param expected_pass: whether the driver is expected to succeed
        """
        command = [cdt_cpp, *leading_args, self.cpp_file, *args]
        res = subprocess.run(command, capture_output=True, cwd=self.work_dir)
        self.handle_test_result(res, expected_pass=expected_pass)

        return res

    def handle_test_result(self, res: subprocess.CompletedProcess, expected_pass=True):
        stdout = res.stdout.decode("utf-8").strip()
        stderr = res.stderr.decode("utf-8").strip()

        P.print(stdout, verbose=True)
        P.print(stderr, verbose=True)

        if expected_pass and res.returncode > 0:
            self.success = False
            raise TestFailure(
                f"{self.fullname} failed with the following stderr {stderr}",
                failing_test=self,
            )

        if not expected_pass and res.returncode == 0:
            self.success = False
            raise TestFailure(
                "expected to fail compilation/linking but didn't.", failing_test=self
            )

        if not self.test_json.get("expected"):
            self.success = True
        else:
            self.handle_expecteds(res)

    def handle_expecteds(self, res: subprocess.CompletedProcess):
        expected = self.test_json["expected"]

        if expected.get("exit-code"):
            exit_code = expected["exit-code"]

            if res.returncode != exit_code:
                self.success = False
                raise TestFailure(
                    f"expected {exit_code} exit code but got {res.returncode}",
                    failing_test=self,
                )

        if expected.get("stderr"):
            expected_stderr = expected["stderr"]
            actual_stderr = res.stderr.decode("utf-8")
            patterns = expected_stderr if isinstance(expected_stderr, list) else [expected_stderr]

            for pat in patterns:
                if pat not in actual_stderr and not re.search(pat, actual_stderr, flags=re.S):
                    self.success = False
                    raise TestFailure(
                        f"expected {pat} stderr but got {actual_stderr}",
                        failing_test=self,
                    )

        if expected.get("abi") or expected.get("abi-file"):
            if expected.get("abi"):
                expected_abi = expected["abi"]
            else:
                full_path = os.path.join(self.test_suite.directory, expected["abi-file"])
                expected_abi_file = open(full_path)
                expected_abi = expected_abi_file.read()
                expected_abi_file.close()

            with open(self.out_abi) as f:
                actual_abi = f.read()

                expected_abi_str = json.dumps(json.loads(expected_abi), indent=2)
                actual_abi_str = json.dumps(json.loads(actual_abi), indent=2)

                if expected_abi_str != actual_abi_str:
                    d = difflib.Differ()
                    diff = d.compare(
                        expected_abi_str.splitlines(), actual_abi_str.splitlines()
                    )
                    P.print("\n".join(diff), verbose=True)
                    self.success = False
                    raise TestFailure(
                        "actual abi did not match expected abi", failing_test=self
                    )

        if expected.get("wasm"):
            expected_wasm = expected["wasm"]

            xxd = subprocess.Popen(("xxd", "-p", self.out_wasm), stdout=subprocess.PIPE)
            tr = subprocess.check_output(("tr", "-d", "\n"), stdin=xxd.stdout)
            xxd.wait()

            actual_wasm = tr.decode("utf-8")

            if expected_wasm != actual_wasm:
                self.success = False
                raise TestFailure(
                    "actual wasm did not match expected wasm", failing_test=self
                )

        self.success = True

    def __repr__(self):
        return self.__str__()

    def __str__(self):
        return self.fullname


COMPILE_ONLY_FLAGS = ["-c"]
ABIGEN_FLAGS = ["-abigen_output=''"]


class BuildPassTest(Test):
    """Compiles and links; expected to succeed."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, [], args)


class CompilePassTest(Test):
    """Compiles only; expected to succeed."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, COMPILE_ONLY_FLAGS, args)


class AbigenPassTest(Test):
    """Generates the ABI; expected to succeed."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, ABIGEN_FLAGS, args)


class BuildFailTest(Test):
    """Compiles and links; expected to fail."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, [], args, expected_pass=False)


class CompileFailTest(Test):
    """Compiles only; expected to fail."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, COMPILE_ONLY_FLAGS, args, expected_pass=False)


class AbigenFailTest(Test):
    """Generates the ABI; expected to fail."""

    def _run(self, cdt_cpp, args):
        return self._run_cdt_cpp(cdt_cpp, ABIGEN_FLAGS, args, expected_pass=False)
