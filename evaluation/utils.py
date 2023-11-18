def get_hex_btwn(s: str, before: str, after: str):
    s = s[s.find(before) + len(before) :]
    return int(s[: s.find(after)], 16)


def get_dec_btwn(s: str, before: str, after: str):
    s = s[s.find(before) + len(before) :]
    return int(s[: s.find(after)])


def get_hex_after(s: str, before: str, hex_len: int = 6):
    s = s[s.find(before) + len(before) :]
    return int(s[:hex_len], 16)


def get_dec_after(s: str, before: str):
    return int(s[s.find(before) + len(before)])


def get_str_btwn(s: str, before: str, after: str):
    s = s[s.find(before) + len(before) :]
    return s[: s.find(after)]


def get_nth_str(s: str, n: int, delim: str = " "):
    return s.split(delim)[n]


def get_str_after(s: str, before: str) -> str:
    return s[s.find(before) + len(before) :]
