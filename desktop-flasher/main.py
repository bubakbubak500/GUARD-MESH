# SPDX-License-Identifier: GPL-3.0-or-later
"""Native application and private subprocess entry point. Never opens a browser."""
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description="Guard-Mesh-Flasher")
    parser.add_argument("--worker", help=argparse.SUPPRESS)
    parser.add_argument("--smoke-test", metavar="REPORT", help="Ověření GUI bez připojení k rádiu")
    parser.add_argument("--data-dir", help="Samostatný adresář knihovny a protokolů")
    args = parser.parse_args()
    if args.worker:
        from guard_mesh_flasher.transport import worker
        return worker(args.worker)
    from guard_mesh_flasher.app import run
    return run(args.data_dir, args.smoke_test)


if __name__ == "__main__":
    sys.exit(main())
