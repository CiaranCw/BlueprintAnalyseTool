"""Agent-side tools for Blueprint IR.

Public entry points:
    QueryAPI         -- high-level Agent-facing facade
    SchemaValidator  -- JSON Schema validation
    UERunner         -- launches UnrealEditor-Cmd Commandlet
"""

from .query_api import QueryAPI
from .schema_validator import SchemaValidator
from .ue_runner import UERunner

__all__ = ["QueryAPI", "SchemaValidator", "UERunner"]
__version__ = "0.1.0"
SCHEMA_VERSION = "0.1.0"
