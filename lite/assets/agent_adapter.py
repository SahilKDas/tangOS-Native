"""Inject repository instructions into the declared Python matching driver.

This adapter supports drivers exposing main() and a standing instruction string.
It fails closed rather than launching a driver that would ignore AGENTS.md.
"""
import importlib.util
import os
import pathlib
import sys


def main():
    driver = pathlib.Path(sys.argv[1]).resolve()
    instructions = pathlib.Path(os.environ["TANGOS_AGENT_INSTRUCTIONS"]).read_text(encoding="utf-8")
    sys.path.insert(0, str(driver.parent))
    spec = importlib.util.spec_from_file_location("tangos_lite_driver", driver)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    injected = False
    for name in ("INSTRUCTIONS", "SYSTEM_PROMPT", "SYSTEM"):
        value = getattr(module, name, None)
        if isinstance(value, str):
            setattr(module, name, instructions + "\n\n" + value)
            injected = True
    if not injected or not callable(getattr(module, "main", None)):
        raise RuntimeError("Driver must expose main() and INSTRUCTIONS/SYSTEM_PROMPT/SYSTEM, or declare a {prompt} argument in tangos.json")
    sys.argv = [str(driver)] + sys.argv[2:]
    result = module.main()
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    sys.exit(main())
