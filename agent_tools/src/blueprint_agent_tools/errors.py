"""Error code registry.

Keep in sync with:
    agent_tools/schemas/error_codes.schema.json
    ue_plugin/.../Public/BPATErrorCodes.h
    docs/agent_tools.md §4
"""

from __future__ import annotations
from dataclasses import dataclass


@dataclass(frozen=True)
class ErrorCode:
    code: str
    name: str
    description: str


ASSET_NOT_FOUND           = ErrorCode("E1001", "ASSET_NOT_FOUND",           "asset_path 不存在")
IR_NOT_DUMPED             = ErrorCode("E1002", "IR_NOT_DUMPED",             "IR 未在 OutputDir 中找到")
SCHEMA_VERSION_MISMATCH   = ErrorCode("E1003", "SCHEMA_VERSION_MISMATCH",   "IR schema 版本不匹配")
NODE_NOT_FOUND            = ErrorCode("E1101", "NODE_NOT_FOUND",            "node_id 不在该蓝图")
VARIABLE_NOT_FOUND        = ErrorCode("E1102", "VARIABLE_NOT_FOUND",        "变量名不在 manifest")
SLICE_DEPTH_EXCEEDED      = ErrorCode("E1201", "SLICE_DEPTH_EXCEEDED",      "切片深度超出上限")

READ_ONLY_VIOLATION       = ErrorCode("E2001", "READ_ONLY_VIOLATION",       "解析期间检测到写入")
OUTPUT_DIR_INSIDE_PROJECT = ErrorCode("E2002", "OUTPUT_DIR_INSIDE_PROJECT", "OutputDir 在 ProjectPath 内")
OVERWRITE_BLOCKED         = ErrorCode("E2003", "OVERWRITE_BLOCKED",         "策略=fail 且输出冲突")

UE_LAUNCH_FAILED          = ErrorCode("E5001", "UE_LAUNCH_FAILED",          "UnrealEditor-Cmd 启动失败")
UE_TIMEOUT                = ErrorCode("E5002", "UE_TIMEOUT",                "dump 超时")


class BPATError(Exception):
    """Base exception carrying a stable error code."""

    def __init__(self, code: ErrorCode, message: str = "", where: dict | None = None):
        super().__init__(f"{code.code} {code.name}: {message}")
        self.code = code
        self.message = message or code.description
        self.where = where or {}

    def to_dict(self) -> dict:
        return {"code": self.code.code, "message": self.message, "where": self.where}
