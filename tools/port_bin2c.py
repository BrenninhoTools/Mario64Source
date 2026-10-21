import sys


def main():
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "w") as f:
        f.write("".join("0x%X," % b for b in data))
        f.write("\n")


if __name__ == "__main__":
    main()
