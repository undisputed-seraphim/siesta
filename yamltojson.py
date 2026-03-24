#!/usr/bin/env python3

from datetime import datetime, date
import yaml
import json
import sys

def datetime_handler(obj):
    if isinstance(obj, (datetime, date)):
        return obj.isoformat()
    raise TypeError(f"Object of type {type(obj).__name__} is not JSON serializable")

def main():
    with open(sys.argv[1], 'r', encoding="utf-8") as f:
        y = yaml.load(f, Loader=yaml.Loader)
        with open(sys.argv[2], 'w', encoding="utf-8") as fo:
            json.dump(y, fo, indent='\t', default=datetime_handler)

if __name__ == "__main__":
    main()
