import logging
from typing import Generic, TypeVar

LOGGER = logging.getLogger()
LOGGER.setLevel(logging.DEBUG)
handler = logging.StreamHandler()
handler.setLevel(logging.DEBUG)
LOGGER.addHandler(handler)

T = TypeVar("T")


class Node(Generic[T]):
    def __init__(self, address: int, value: T | None):
        self.children: dict[int, Node[T]] = {}
        self.address = address
        self.value = value

    def __str__(self):
        return f"{self.address} - {self.value} - {self.children}"


class Trie(Generic[T]):
    def __init__(self):
        self.root = Node[T](0, None)

    def __str__(self) -> str:
        indent = ""
        return self._str(self.root, indent)

    def _str(self, node: Node[T], indent: str) -> str:
        s = f"{indent} [{node.address}] {node.value}\n"

        indent += "  "
        for child in node.children.values():
            s += self._str(child, indent)

        return s

    def values(self) -> list[T | None]:
        return self._values(self.root)

    def _values(self, node: Node[T]) -> list[T | None]:
        node_values = [node.value]
        for child in node.children.values():
            node_values.extend(self._values(child))
        return node_values

    def keys(self) -> list[list[int]]:
        return self._keys(self.root, [])

    def _keys(self, node: Node[T], cur_key: list[int]) -> list[list[int]]:
        nodes = [cur_key + [node.address]]
        for trace, child in node.children.items():
            nodes.extend(self._keys(child, cur_key + [trace]))
        return nodes

    def get_node(self, trace: list[int]) -> Node[T] | None:
        return self.__get_node(self.root, trace)

    def __get_node(self, node: Node[T], trace: list[int]) -> Node[T] | None:
        if len(trace) == 1:
            return node.children[trace[0]]
        else:
            return self.__get_node(node.children[trace[0]], trace[1:])

    def insert_value(self, trace: list[int], value: T):
        success = self.__insert_value(self.root, trace, value)
        if success:
            msg = f"Inserted node with trace {[hex(x) for x in trace]} into trie"
            LOGGER.debug(msg)

    def __insert_value(self, node: Node[T], trace: list[int], value: T) -> bool:
        if len(trace) == 1:
            if trace[0] not in node.children:
                node.children[trace[0]] = Node[T](trace[0], value)
                return True
            else:
                return False
        else:
            if trace[0] not in node.children:
                node.children[trace[0]] = Node[T](trace[0], None)
            return self.__insert_value(node.children[trace[0]], trace[1:], value)

    def remove_node(self, trace: list[int]):
        self.__remove_node(trace, self.root)

    def __remove_node(self, trace: list[int], cur_node: Node[T]):
        if len(trace) == 1:
            del cur_node.children[trace[0]]
        else:
            self.__remove_node(trace[1:], cur_node.children[trace[0]])

    def insert_node(self, trace: list[int], node: Node[T]):
        self.__insert_node(trace, node, self.root)

    def __insert_node(
        self,
        trace: list[int],
        node_to_insert: Node[T],
        node_in_trie: Node[T],
    ):
        if len(trace) == 1:
            node_in_trie.children[trace[0]] = node_to_insert
        else:
            self.__insert_node(
                trace[1:],
                node_to_insert,
                node_in_trie.children[trace[0]],
            )
