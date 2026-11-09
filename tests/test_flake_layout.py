#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception
"""Source-only regression checks for Gateway's public Nix interface.

These checks supplement, but do not replace, `nix flake show` and
`nix flake check` on a host with Nix installed.
"""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parent.parent
FLAKE = (REPO / "flake.nix").read_text()


def dos_launcher_script():
    return (REPO / "tools/gateway_dos.sh").read_text()


class FlakeLayoutTests(unittest.TestCase):
    def test_only_the_four_public_game_modes_are_apps(self):
        apps = FLAKE.split("      apps = eachSystem", 1)[1].split(
            "      checks = eachSystem", 1)[0]
        exported = set(re.findall(r"(?m)^        ([a-z][a-z-]*) =", apps))
        self.assertEqual(exported, {
            "default", "gateway", "gateway-dos", "gateway-decomp", "gateway-hybrid"
        })
        self.assertIn("default = gatewayApp;", apps)
        self.assertIn("gateway = gatewayApp;", apps)
        self.assertEqual(apps.count('meta.description = "'), 4)
        self.assertIn("/bin/gateway-intro-source-c-live", apps)
        self.assertIn("/bin/gateway-intro-source-c-play", apps)
        self.assertIn('tools/gateway_dos.sh', apps)
        self.assertIn('export GATEWAY_MSC51_DIR="${self.packages.${system}.msc51}"', apps)
        self.assertIn('export GATEWAY_MSC51_SOURCE_DIR="${self}"', apps)
        self.assertIn('pkgs.dosbox-staging pkgs.xvfb-run', apps)
        self.assertIn('exec "$dosbox_bin"', dos_launcher_script())
        self.assertIn('GATEWAY_MSC51_BUILD_DIR', dos_launcher_script())
        self.assertIn('mount e', dos_launcher_script())

    def test_research_launchers_are_shell_commands(self):
        shell = FLAKE.split("      devShells = eachSystem", 1)[1]
        targets = re.search(r"devUtilities = map devUtility \[(.*?)\];", shell, re.S)
        self.assertIsNotNone(targets)
        names = re.findall(r'"(gateway-[a-z-]+)"', targets.group(1))
        self.assertEqual(len(names), len(set(names)))
        cmake = (REPO / "CMakeLists.txt").read_text()
        for name in names:
            self.assertIn(f"add_executable({name}", cmake)
        self.assertIn('"gateway-intro-source-c-full"', shell)
        self.assertIn('"gateway-msc51"', shell)
        self.assertIn('pkgs.dosbox-staging pkgs.xvfb-run', shell)
        self.assertIn('"gateway-decompile-intro"', FLAKE)
        for heading in ("Build and run", "Source-C intro development",
                        "Emulation diagnostics and captures", "Specialized tools"):
            self.assertIn(heading, shell)

    def test_welcome_text_does_not_end_nix_multiline_strings(self):
        # Inside a Nix indented string, two consecutive single quotes are
        # a Nix delimiter, not an empty Bash argument.  Use "" for blank
        # lines in the shellHook's printf argument list instead.
        shell = FLAKE.split("      devShells = eachSystem", 1)[1]
        self.assertNotRegex(shell, r"(?m)^[ \t]*''[ \t]*\\[ \t]*$")
        self.assertNotRegex(shell, r"(?m)^[ \t]*''[ \t]*$")
        self.assertIn('"" \\\n                    \'Build and run', shell)
        self.assertIn('""\n                  ;;', shell)
        self.assertNotIn(r"printf '\n%s\n'", shell)
        self.assertEqual(shell.count(r"printf '%s\n'"), 3)

    def test_launcher_shell_syntax(self):
        for filename in ("gateway_dos.sh", "gateway_intro_source_c_full.sh"):
            result = subprocess.run(["bash", "-n", str(REPO / "tools" / filename)],
                                    text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, f"{filename}: {result.stderr}")

    def test_dos_launcher_builds_then_caches_real_mode_exe(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            game = root / "game assets"
            build = root / "rebuilt exe"
            bin_dir = root / "bin"
            compiler = root / "compiler"
            for directory in (game, build, bin_dir, compiler / "BIN"):
                directory.mkdir(parents=True)
            (game / "GATE.EXE").write_bytes(b"test original EXE")
            (game / "GATE.RTL").write_bytes(b"test original RTL")
            exe = bin_dir / "dosbox-staging"
            exe.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$DOSBOX_ARGS\"\n")
            exe.chmod(0o755)
            python = bin_dir / "python3"
            python.write_text(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$2\" >> \"$MSC51_CALLS\"\n"
                "if [ \"$2\" = compile-all ]; then\n"
                "  printf MZ > \"$GATEWAY_MSC51_BUILD_DIR/GATEWAY.EXE\"\n"
                "fi\n"
            )
            python.chmod(0o755)
            args_file = root / "args.txt"
            calls_file = root / "calls.txt"
            env = dict(os.environ, GATEWAY_MSC51_BUILD_DIR=str(build),
                       GATEWAY_MSC51_DIR=str(compiler),
                       MSC51_CALLS=str(calls_file),
                       DOSBOX_ARGS=str(args_file), PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
            run = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                  str(game), "/PICTURE"], env=env,
                                 text=True, capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(calls_file.read_text().splitlines(), ["compile-all"])
            self.assertEqual(args_file.read_text().splitlines(), [
                "--set", "core=dynamic",
                "--set", "cpu_cycles=max",
                "-c", f'mount e "{game}" -ro',
                "-c", f'mount d "{build}"',
                "-c", "e:",
                "-c", r"D:\GATEWAY.EXE /PICTURE",
                "--exit",
            ])
            run = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                  str(game), "/INTRO"], env=env,
                                 text=True, capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(calls_file.read_text().splitlines(), ["compile-all"])
            self.assertIn(r"D:\GATEWAY.EXE /INTRO", args_file.read_text())
            for probe in ("/TRANS", "/SCENE"):
                run = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                      str(game), probe], env=env,
                                     text=True, capture_output=True, check=False)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn("-c\n" + rf"D:\GATEWAY.EXE {probe}" + "\n",
                              args_file.read_text())
                self.assertEqual(calls_file.read_text().splitlines(), ["compile-all"])
            rebuild_env = dict(env, GATEWAY_DOS_REBUILD="1")
            run = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                  str(game), "/CHECK"], env=rebuild_env,
                                 text=True, capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(calls_file.read_text().splitlines(), ["compile-all"] * 2)
            changed_source = root / "second source revision"
            changed_source.mkdir()
            changed_env = dict(env, GATEWAY_MSC51_SOURCE_DIR=str(changed_source))
            run = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                  str(game), "/CHECK"], env=changed_env,
                                 text=True, capture_output=True, check=False)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(calls_file.read_text().splitlines(), ["compile-all"] * 3)
            args_file.unlink()
            bad = subprocess.run(["bash", "-c", dos_launcher_script(), "--",
                                  str(game), "/UNKNOWN"], env=env,
                                 text=True, capture_output=True, check=False)
            self.assertEqual(bad.returncode, 2)
            self.assertFalse(args_file.exists())


if __name__ == "__main__":
    unittest.main()
