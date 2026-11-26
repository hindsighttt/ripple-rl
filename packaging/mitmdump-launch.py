# Portable mitmdump entry point for the bundled runtime.
# ProxyManager runs:  runtime\python.exe  runtime\mitmdump-launch.py  <args>
# which avoids shipping a pip-generated mitmdump.exe (those embed an absolute
# path to the Python that created them and are not relocatable).
import sys
from mitmproxy.tools.main import mitmdump

if __name__ == "__main__":
    sys.exit(mitmdump())
