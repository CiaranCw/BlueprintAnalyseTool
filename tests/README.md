# 测试目录

| 子目录                | 内容                                               |
| --------------------- | -------------------------------------------------- |
| `ir_validation/`      | IR Schema 校验 + golden IR 对比                    |
| `readonly_safety/`    | 跑过一次 dump 后比对工程指纹是否改变               |
| `slicing/`            | 切片正确性（不需要 UE，纯 IR fixture）             |
| `batch/`              | 批量解析回归                                        |

> Python 单元测试统一放在 `agent_tools/tests/` 下；本目录用于跨语言 / 端到端 / 需要
> 真实 UE 工程的测试。

## 运行

```powershell
# 仅 Python 单测（不需 UE）
cd agent_tools
pip install -e .[dev]
pytest

# 端到端只读安全
python ..\scripts\verify_readonly.py snapshot D:\MyGame --out before.json
.. (run dump_blueprint.ps1)
python ..\scripts\verify_readonly.py snapshot D:\MyGame --out after.json
python ..\scripts\verify_readonly.py diff before.json after.json
```
