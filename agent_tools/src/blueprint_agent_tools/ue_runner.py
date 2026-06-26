"""Wrapper around UnrealEditor-Cmd.exe for invoking BPATDump commandlet.

This is the only Python module that may launch UE. All other Agent-facing
APIs default to OFFLINE.
"""

from __future__ import annotations
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .errors import BPATError, UE_LAUNCH_FAILED, UE_TIMEOUT


@dataclass
class UERunnerConfig:
    """Where UnrealEditor-Cmd lives. Configure once per machine."""
    unreal_editor_cmd: str = ""        # absolute path to UnrealEditor-Cmd.exe
    default_timeout_sec: int = 60 * 30


class UERunner:
    def __init__(self, config: UERunnerConfig | None = None) -> None:
        self.config = config or UERunnerConfig(
            unreal_editor_cmd=os.environ.get("UNREAL_EDITOR_CMD", "")
        )

    def _check(self) -> None:
        if not self.config.unreal_editor_cmd or not Path(self.config.unreal_editor_cmd).is_file():
            raise BPATError(UE_LAUNCH_FAILED,
                            f"UnrealEditor-Cmd not found: {self.config.unreal_editor_cmd!r}. "
                            f"Set UNREAL_EDITOR_CMD env var or pass UERunnerConfig.")

    def dump_blueprint(
        self,
        project: str,
        asset_path: str,
        output_dir: str,
        overwrite: str = "skip",
        layers: list[str] | None = None,
        dry_run: bool = False,
        timeout_sec: int | None = None,
    ) -> dict:
        self._check()
        cmd = [
            self.config.unreal_editor_cmd,
            project,
            "-run=BPATDump",
            f"-AssetPath={asset_path}",
            f"-OutputDir={output_dir}",
            f"-OverwritePolicy={overwrite}",
            f"-DryRun={int(dry_run)}",
            "-StrictReadOnly=1",
        ]
        if layers:
            cmd.append(f"-Layers={','.join(layers)}")

        return self._run(cmd, timeout_sec)

    def dump_project_blueprints(
        self,
        project: str,
        output_dir: str,
        class_filter: list[str] | None = None,
        overwrite: str = "skip",
        timeout_sec: int | None = None,
    ) -> dict:
        self._check()
        cmd = [
            self.config.unreal_editor_cmd,
            project,
            "-run=BPATDump",
            "-ProjectScan=1",
            f"-OutputDir={output_dir}",
            f"-OverwritePolicy={overwrite}",
            "-StrictReadOnly=1",
        ]
        if class_filter:
            cmd.append(f"-ClassFilter={','.join(class_filter)}")

        return self._run(cmd, timeout_sec)

    def _run(self, cmd: list[str], timeout_sec: int | None) -> dict:
        try:
            res = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout_sec or self.config.default_timeout_sec,
            )
        except subprocess.TimeoutExpired as ex:
            raise BPATError(UE_TIMEOUT, str(ex)) from ex

        return {
            "exit_code": res.returncode,
            "stdout":    res.stdout,
            "stderr":    res.stderr,
        }
