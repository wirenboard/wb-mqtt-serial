#!/usr/bin/env python3

import sys
import os
from jinja2 import FileSystemLoader, Environment

if len(sys.argv) < 2:
    print('Usage: template-generator.py TEMPLATE_PATH RESULT_PATH')
    exit(1)

loader = FileSystemLoader(os.path.dirname(sys.argv[1]))
env = Environment(loader=loader)
template = env.get_template(os.path.basename(sys.argv[1]))
open(sys.argv[2], 'w', encoding='utf-8').write(template.render())
