"""Send one leased controller action or head pose to Elliott's running simulator."""
import argparse
import math
import time

import run_blvr_elliott_proof as elliott


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    button = commands.add_parser("button")
    button.add_argument("name", choices=("a", "b", "x", "y", "menu"))
    button.add_argument("--milliseconds", type=int, default=180)
    stick = commands.add_parser("stick")
    stick.add_argument("hand", choices=("left", "right"))
    stick.add_argument("x", type=float)
    stick.add_argument("y", type=float)
    stick.add_argument("--milliseconds", type=int, default=500)
    head = commands.add_parser("head")
    head.add_argument("--yaw", type=float, default=0, help="Degrees")
    head.add_argument("--pitch", type=float, default=0, help="Degrees")
    head.add_argument("--roll", type=float, default=0, help="Degrees")
    head.add_argument("--x", type=float, default=0)
    head.add_argument("--y", type=float, default=1.7)
    head.add_argument("--z", type=float, default=0)
    commands.add_parser("release")
    args = parser.parse_args()
    sequence = int(time.time() * 1000) & 0x7fffffff
    if args.command == "head":
        elliott.send_head_pose(x=args.x, y=args.y, z=args.z,
                              yaw=math.radians(args.yaw), pitch=math.radians(args.pitch),
                              roll=math.radians(args.roll))
    elif args.command == "release":
        elliott.send_controller(sequence, 2, neutral=True, lease_ms=500)
    else:
        if not 50 <= args.milliseconds <= 5000:
            parser.error("Lease must be between 50 and 5000 milliseconds")
        if args.command == "button":
            hand, field = {"a": (1, "primary"), "b": (1, "secondary"),
                           "x": (0, "primary"), "y": (0, "secondary"),
                           "menu": (0, "menu")}[args.name]
            elliott.send_controller(sequence, hand, lease_ms=args.milliseconds, **{field: True})
        else:
            if not -1 <= args.x <= 1 or not -1 <= args.y <= 1:
                parser.error("Stick axes must be between -1 and 1")
            elliott.send_controller(sequence, 0 if args.hand == "left" else 1,
                                    lease_ms=args.milliseconds, stickX=args.x, stickY=args.y)
    print("Simulator acknowledged the command.")


if __name__ == "__main__":
    main()
