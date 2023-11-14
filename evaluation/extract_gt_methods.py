"""
Extract ground truth methods from the json file specified in
the configuration file passed in as an argument to this script.
"""

import json

from parseconfig import Config


def main(cfg: Config):
    base_addr = int(cfg.base_offset_path.open().readline(), 16)

    structures = json.load(cfg.gt_results_json.open())["structures"]

    method_addrs: set[int] = set()

    for cls in structures.values():
        for method in cls["methods"].values():
            ea = method["ea"]
            method_addrs.add(int(ea, 16) - base_addr)

    with cfg.gt_methods_path.open("w") as f:
        for method in method_addrs:
            f.write(hex(method)[2:] + "\n")
