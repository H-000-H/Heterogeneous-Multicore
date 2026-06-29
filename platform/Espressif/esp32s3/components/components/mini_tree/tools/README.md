# mini_tree 构建工具

## dtc-lite

MCU 编译期 DeviceTree 编译器：`DTS → board_devtable.c / board_probe.c / dt_config_gen.h`。

```bash
python dtc-lite.py <board.dts> <output_dir> [driver_source_dirs...]
```

### 包结构

| 模块 | 职责 |
|------|------|
| `dtc_lite/lexer.py` | PLY 词法分析 |
| `dtc_lite/parser.py` | 递归下降语法分析（消费 lexer token 流） |
| `dtc_lite/compiler.py` | `#include` 预处理、overlay 合并、驱动扫描 |
| `dtc_lite/generator.py` | C 代码生成 |
| `vendor/ply/` | vendored [PLY](https://github.com/dabeaz/ply) |

设备树编写规范见 `board/docs/devicetree.md`。
