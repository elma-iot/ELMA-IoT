"""Allowlisted LED modules: emitted by ELMA's configuration compiler."""
import os
from pathlib import Path

def apply(env):
    mask=int(os.environ.get('ELMA_LED_EFFECT_MASK','1023'))
    if mask<1 or mask>65535 or not mask&1:raise ValueError('Invalid LED effect module mask')
    # A scoped header avoids rebuilding every unrelated library when only
    # the selected effects change. Only LED consumers depend on this file.
    directory=Path(env.subst('$PROJECT_DIR'))/'include'
    directory.mkdir(parents=True,exist_ok=True)
    target=directory/'elma_led_modules.h'
    content=f'#pragma once\n#define ELMA_LED_EFFECT_MASK {mask}\n'
    if not target.exists() or target.read_text()!=content:target.write_text(content)
