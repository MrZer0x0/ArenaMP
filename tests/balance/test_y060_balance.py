#!/usr/bin/env python3
"""Compatibility shim: Y060 was superseded by Y061.

Kept at the old path so overlay-style cumulative updates do not leave a stale
Y060 assertion suite in existing repositories.
"""
from pathlib import Path
import runpy

runpy.run_path(str(Path(__file__).with_name('test_y061_balance.py')), run_name='__main__')
