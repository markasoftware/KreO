from dataclasses import dataclass
from enum import StrEnum, auto


class Type(StrEnum):
    dtor = auto()
    ctor = auto()
    meth = auto()


@dataclass
class Method:
    address: int
    found_dynamically: bool = True
    name: str = ""

    type: Type = Type.meth
    is_initializer: bool = False
    is_finalizer: bool = False

    seen_in_head: int = 0
    seen_in_tail: int = 0
    seen_count: int = 0

    def reset_method_statistics(self):
        self.seen_in_head = 0
        self.seen_in_tail = 0
        self.seen_count = 0

    def __str__(self) -> str:
        return hex(self.address)[2:]

    def __hash__(self) -> int:
        return self.address
