#!/bin/zsh
SCRIPT_DIR=${0:A:h}
if command -v python3 >/dev/null 2>&1; then
  python3 "$SCRIPT_DIR/download_light_sleep_test.py"
elif [[ -x "$HOME/.platformio/penv/bin/python3" ]]; then
  "$HOME/.platformio/penv/bin/python3" "$SCRIPT_DIR/download_light_sleep_test.py"
else
  print 'Python 3 is required. Use the Python that came with your firmware build tools.'
fi
print ''
read 'REPLY?Press Return to close.'
