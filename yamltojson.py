#!/usr/bin/env python3

import yaml
import json
import sys

def main():
    with open(sys.argv[1], 'r', encoding="utf-8") as f:
        y = yaml.load(f, Loader=yaml.Loader)
        with open(sys.argv[2], 'w', encoding="utf-8") as fo:
            fo.write(json.dumps(y, indent='\t', ensure_ascii=False))

if __name__ == "__main__":
    main()
