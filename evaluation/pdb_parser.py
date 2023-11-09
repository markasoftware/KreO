from collections import defaultdict
from enum import Enum, auto
from pathlib import Path
from typing import cast

from pydantic import BaseModel, Field
from typing_extensions import Any, Self

import evaluation.utils as utils
import postgame.analysis_results as ar

BASE_ADDR = 0x400000


class TypeId(Enum):
    MEMBER_FUNCTION = (auto(), ["LF_MFUNCTION"])
    CLASS = (auto(), ["LF_CLASS", "LF_STRUCTURE"])
    FIELD_LIST = (auto(), ["LF_FIELDLIST"])
    MODIFIER = (auto(), ["LF_MODIFIER"])
    ENUM = (auto(), ["LF_ENUM"])
    POINTER = (auto(), ["LF_POINTER"])
    ARRAY = (auto(), ["LF_ARRAY"])
    UNION = (auto(), ["LF_UNION"])
    VTSHAPE = (auto(), ["LF_VTSHAPE"])
    ARGLIST = (auto(), ["LF_ARGLIST"])
    METHOD_ILST = (auto(), ["LF_METHODLIST"])
    UNKNOWN = (auto(), ["LF_MFUNCTION"])
    PROCEDURE = (auto(), ["LF_PROCEDURE"])
    BITFIELD = (auto(), ["LF_BITFIELD"])

    @classmethod
    def from_str(cls, s: str) -> Self:
        for type_id in cls.__members__.values():
            if s in cast("Self", type_id).value[1]:
                return type_id
        msg = f"failed to convert string {s} to a type id"
        raise ValueError(msg)


class TypeData(BaseModel):
    type: int
    length: int
    leaf: int

    @classmethod
    def parse_first_line(cls, line: str) -> dict[str, int]:
        return {
            "type": int(line.split()[0], 16),
            "length": utils.get_dec_btwn(line, "Length = ", ","),
            "leaf": utils.get_hex_btwn(line, "Leaf = ", " "),
        }


class ClassTypeData(TypeData):
    members: int
    field_list_type: int
    derivation_list_type: int
    vt_shape_type: int
    size: int
    class_name: str
    unique_name: str
    forward_ref: bool = False

    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        members = cast("dict[str, Any]", TypeData.parse_first_line(lines[0]))
        members["members"] = utils.get_dec_btwn(lines[1], "members = ", ",  ")
        members["field_list_type"] = utils.get_hex_after(lines[1], "field list type ")
        members["derivation_list_type"] = utils.get_hex_after(
            lines[2], "Derivation list type "
        )
        members["vt_shape_type"] = utils.get_hex_after(lines[2], "VT shape type ")
        members["size"] = utils.get_dec_btwn(lines[3], "Size = ", ",")
        members["class_name"] = utils.get_str_btwn(
            lines[3],
            ", class name = ",
            ", unique name",
        )
        members["unique_name"] = utils.get_str_btwn(
            lines[3],
            ", unique name = ",
            ", UDT(",
        )
        members["forward_ref"] = "FORWARD REF" in utils.get_str_after(
            lines[1],
            "field list type ",
        )

        return super().model_validate(
            members,
            strict=strict,
            from_attributes=from_attributes,
            context=context,
        )


class FieldListTypeData(TypeData):
    base_classes: set[int]

    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        members = cast("dict[str, Any]", TypeData.parse_first_line(lines[0]))
        members["base_classes"] = set()

        for line in lines:
            line_split = line.split(",")
            identifier = line_split[0].split("=")[1].strip()
            if identifier == "LF_BCLASS":
                type = utils.get_hex_after(line_split[2], "type = ")
                cast("set[int]", members["base_classes"]).add(type)

            identifier = utils.get_nth_str(line, 2)

        return super().model_validate(
            members,
            strict=strict,
            from_attributes=from_attributes,
            context=context,
        )


class MethodListTypeData(TypeData):
    method_list: set[int]

    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        members = cast("dict[str, Any]", TypeData.parse_first_line(lines[0]))
        members["method_list"] = []

        for line in lines:
            method_parameters: list[str] = []

            i = 1
            try:
                while True:
                    param = utils.get_nth_str(line, i, ",")
                    i += 1
                    method_parameters.append(param)
            except:
                pass

            method_type = method_parameters[0]
            if (
                method_type == "VANILLA"
                or method_type == "VIRTUAL"
                or method_type == "INTRODUCING VIRTUAL"
            ):
                pass

            method_type_id = 0

            try:
                method_type_id = int(method_parameters[1].strip(), 16)
            except:
                method_type_id = int(method_parameters[2].strip(), 16)

            cast("list[int]", members["method_list"]).append(method_type_id)

        return super().model_validate(
            members,
            strict=strict,
            from_attributes=from_attributes,
            context=context,
        )


class FuncAttr(Enum):
    """Enum, types associated wtih function attribute in dump file of LF_MFUNCTION."""

    NONE = auto()
    INSTANCE_CONSTRUCTOR = auto()
    RETURN_UDT = auto()
    UNUSED_NONZERO = auto()

    @staticmethod
    def from_str(s: str):
        s = s.strip()

        if s == "none":
            return FuncAttr.NONE
        elif s == "return UDT (C++ style)":
            return FuncAttr.RETURN_UDT
        elif s == "instance constructor":
            return FuncAttr.INSTANCE_CONSTRUCTOR
        elif s == "****Warning**** unused field non-zero!":
            return FuncAttr.UNUSED_NONZERO


class ProcedureData(TypeData):
    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        members = TypeData.parse_first_line(lines[0])

        # TODO

        return super().model_validate(
            members,
            strict=strict,
            from_attributes=from_attributes,
            context=context,
        )


class MethodTypeData(TypeData):
    return_type: str
    class_type_ref: int
    this_type: int | None
    call_type: str
    func_attr: FuncAttr
    params: int
    arg_list_type: int
    this_adjust: int

    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        members = cast("dict[str, Any]", TypeData.parse_first_line(lines[0]))

        members["return_type"] = utils.get_str_btwn(lines[1], "Return type = ", ", ")
        members["class_type_ref"] = utils.get_hex_btwn(lines[1], "Class type = ", ",")

        try:
            members["this_type"] = utils.get_hex_btwn(lines[1], "This type = ", ",")
        except ValueError:
            if utils.get_str_after(lines[1], "This type = ") != "T_NOTYPE(0000), ":
                msg = "failed to find this type and not T_NOTYPE"
                raise RuntimeError(msg)

            members["this_type"] = None

        members["call_type"] = utils.get_str_btwn(lines[2], "Call type = ", ",")
        members["func_attr"] = FuncAttr.from_str(
            utils.get_nth_str(utils.get_nth_str(lines[2], 1, ", "), 1, "=")
        )
        members["params"] = utils.get_dec_btwn(lines[3], "Parms = ", ",")
        members["arg_list_type"] = utils.get_hex_btwn(lines[3], "Arg list type = ", ",")
        members["this_adjust"] = utils.get_dec_after(lines[3], "This adjust = ")

        return super().model_validate(
            members, strict=strict, from_attributes=from_attributes, context=context
        )


class SectionHeaderInfo(BaseModel):
    header_num: int
    virtual_size: int
    virtual_addr: int

    @classmethod
    def model_validate_lines(
        cls: type[Self],
        lines: list[str],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        obj = {
            "header_num": utils.get_hex_after(lines[0], "SECTION HEADER #"),
            "virtual_size": int(lines[2].strip().split(" ")[0], 16),
            "virtual_addr": int(lines[3].strip().split(" ")[0], 16),
        }
        return super().model_validate(
            obj, strict=strict, from_attributes=from_attributes, context=context
        )


class ProcedureSymbolData(BaseModel):
    type_id: int
    section_header: int
    relative_addr: int
    name: str


class PdbParser(BaseModel):
    section_header_map: dict[int, SectionHeaderInfo] = Field(default_factory=dict)
    procedure_list: list[ProcedureSymbolData] = Field(default_factory=list)
    type_to_typedata_map: dict[int, TypeData] = Field(default_factory=dict)

    @classmethod
    def model_validate_dumpfile(
        cls: type[Self],
        dumpfile: Path,
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        with dumpfile.open() as f:
            contents = f.read()

        obj: dict[str, Any] = {
            "section_header_map": PdbParser.parse_section_headers(contents),
            "procedure_list": PdbParser.parse_symbols(contents),
            "type_to_typedata_map": PdbParser.parse_types(contents),
        }

        return super().model_validate(
            obj,
            strict=strict,
            from_attributes=from_attributes,
            context=context,
        )

    @staticmethod
    def parse_types(contents: str) -> dict[int, TypeData]:
        type_id_to_type_data_map: dict[int, TypeData] = {}

        types = PdbParser.__get_types(contents)[1:]

        types = types.split("\n\n")
        type_lines = [x.splitlines() for x in types]
        type_lines = [x for x in type_lines if x != [""]]

        for lines in type_lines:
            type_id = TypeId.from_str(utils.get_nth_str(lines[0], 8))

            type_data = None
            if type_id == TypeId.MEMBER_FUNCTION:
                type_data = MethodTypeData.model_validate_lines(lines)
            if type_id == TypeId.PROCEDURE:
                type_data = ProcedureData.model_validate_lines(lines)
            elif type_id == TypeId.CLASS:
                type_data = ClassTypeData.model_validate_lines(lines)
            elif type_id == TypeId.FIELD_LIST:
                type_data = FieldListTypeData.model_validate_lines(lines)
            # elif type_id == TypeId.UNKNOWN:
            #     msg = f"Unknown type id {type_id}"
            #     raise RuntimeError(msg)
            # else:
            #     msg = f"Invalid type id {type_id}"
            #     raise RuntimeError(msg)

            if type_data is not None:
                type_id_to_type_data_map[type_data.type] = type_data

        return type_id_to_type_data_map

    @staticmethod
    def parse_section_headers(contents: str) -> dict[int, SectionHeaderInfo]:
        section_header_map: dict[int, SectionHeaderInfo] = {}

        section_headers = PdbParser.__get_section_headers(contents)

        sections = section_headers.split("\n\n")

        for sec in sections:
            lines: list[str] = sec.splitlines()

            if len(lines) == 0:
                continue

            section_info = SectionHeaderInfo.model_validate_lines(lines)
            section_header_map[section_info.header_num] = section_info

        return section_header_map

    @staticmethod
    def parse_symbols(contents: str):
        procedure_list: list[ProcedureSymbolData] = []

        symbols = PdbParser.__get_symbols(contents)

        lines = symbols.splitlines()
        lines = [x for x in lines if "** Module:" not in x and x != ""]

        symbol_iter = iter(lines)
        try:
            while True:
                next_symbol = next(symbol_iter)
                while not next_symbol.startswith("("):
                    next_symbol = next(symbol_iter)

                if "S_GPROC32" in next_symbol or "S_LPROC32" in next_symbol:
                    addr_str = utils.get_str_btwn(next_symbol, "[", "]")

                    section_header = utils.get_hex_btwn(addr_str, "", ":")
                    relative_addr = utils.get_hex_after(addr_str, ":", 8)

                    type_str = utils.get_str_btwn(next_symbol, "Type:", ", ")

                    if "T_NOTYPE(0000)" not in type_str:
                        type = int(type_str, 16)
                        name = utils.get_str_after(next_symbol, type_str + ", ")

                        procedure_list.append(
                            ProcedureSymbolData(
                                type_id=type,
                                section_header=section_header,
                                relative_addr=relative_addr,
                                name=name,
                            )
                        )
        except StopIteration:
            pass

        return procedure_list

    @staticmethod
    def __get_types(dumpfile_contents: str):
        return PdbParser.__get_data_between_section_headers(
            "*** TYPES\n",
            "*** TYPES Mismatch Warnings",
            dumpfile_contents,
        )

    @staticmethod
    def __get_symbols(dumpfile_contents: str):
        return PdbParser.__get_data_between_section_headers(
            "*** SYMBOLS\n",
            "*** GLOBALS",
            dumpfile_contents,
        )

    @staticmethod
    def __get_section_headers(dumpfile_contents: str):
        return PdbParser.__get_data_between_section_headers(
            "*** SECTION HEADERS\n",
            "*** ORIGINAL SECTION HEADERS",
            dumpfile_contents,
        )

    @staticmethod
    def __get_data_between_section_headers(
        h1: str,
        h2: str,
        dumpfile_contents: str,
    ) -> str:
        start = dumpfile_contents.find(h1) + len(h1)
        end = dumpfile_contents.find(h2)
        return dumpfile_contents[start:end]


def get_name_namespace_removed(name: str) -> str:
    def _remove_namespace(s: str) -> str:
        s_no_namespace_start = s.rfind("::")
        if s_no_namespace_start == -1:
            return s
        else:
            return s[s_no_namespace_start + 2 :]

    if name[-1] == ">":
        name_reversed = reversed(name)
        template_stack = 0

        # remove template
        i = 0
        for i, ch in zip(range(len(name)), name_reversed):
            if ch == ">":
                template_stack += 1
            elif ch == "<":
                template_stack -= 1

            if template_stack == 0:
                break

        name_no_namespace = _remove_namespace(name[: -i - 1])
        name_no_namespace += name[-i - 1 :]

    else:
        name_no_namespace = _remove_namespace(name)

    return name_no_namespace


def main(pdb_file: Path, results_file: Path):
    parser = PdbParser.model_validate_dumpfile(pdb_file)

    class_to_procedure_list_map: dict[int, list[ProcedureSymbolData]] = defaultdict(
        list
    )

    for procedure_symbol in parser.procedure_list:
        if procedure_symbol.type_id in parser.type_to_typedata_map:
            procedure_type_data = parser.type_to_typedata_map[procedure_symbol.type_id]

            if isinstance(procedure_type_data, MethodTypeData):
                if procedure_type_data.this_type:
                    class_to_procedure_list_map[
                        procedure_type_data.class_type_ref
                    ].append(procedure_symbol)
        else:
            msg = f"Failed to find procedure {procedure_symbol} in typedata map"
            raise RuntimeError(msg)

    results = ar.AnalysisResults()

    unique_name_to_class: dict[str, ClassTypeData] = {}

    for cls in parser.type_to_typedata_map.values():
        if isinstance(cls, ClassTypeData) and not cls.forward_ref:
            unique_name_to_class[cls.unique_name] = cls

    for cls_key, methods in class_to_procedure_list_map.items():
        # members
        if cls_key not in parser.type_to_typedata_map:
            msg = f"failed to find type associated with type id {cls_key}"
            raise RuntimeError(msg)

        forward_ref = parser.type_to_typedata_map[cls_key]

        members: dict[str, ar.Member] = {}

        if isinstance(forward_ref, ClassTypeData):
            if forward_ref.forward_ref:
                field_list = parser.type_to_typedata_map[
                    unique_name_to_class[forward_ref.unique_name].field_list_type
                ]

                if isinstance(field_list, FieldListTypeData):
                    for i, base_class_type in zip(
                        range(len(field_list.base_classes)),
                        field_list.base_classes,
                    ):
                        base_class = parser.type_to_typedata_map[base_class_type]

                        if isinstance(base_class, ClassTypeData):
                            members[str(i)] = ar.Member(
                                name=base_class.class_name,
                                struc=base_class.unique_name,
                            )
                        else:
                            msg = f"Expected class type, {base_class}"
                    field_list.base_classes
                else:
                    msg = f"Expected field list, {field_list}"
                    raise RuntimeError(msg)
            else:
                msg = f"Expected forward ref class, {forward_ref}"
                raise RuntimeError(msg)
        else:
            msg = f"Type incorrect, expected ClassTypeData: {forward_ref}"
            raise RuntimeError(msg)

        structure = ar.Structure(
            name=forward_ref.unique_name,
            demangled_name=forward_ref.class_name,
            members=members,
        )

        # methods
        for method in methods:
            ea = (
                parser.section_header_map[method.section_header].virtual_addr
                + method.relative_addr
                + BASE_ADDR
            )

            class_name_no_namespace = get_name_namespace_removed(forward_ref.class_name)
            method_name_no_namespace = get_name_namespace_removed(method.name)

            if class_name_no_namespace == method_name_no_namespace:
                method_type = ar.MethodType.ctor
            elif "~" + class_name_no_namespace == method_name_no_namespace:
                method_type = ar.MethodType.dtor
            else:
                method_type = ar.MethodType.meth

            structure.methods[hex(ea)] = ar.Method(
                demangled_name=method.name,
                ea=hex(ea),
                name=method.name,
                type=method_type,
            )

        results.structures[forward_ref.unique_name] = structure

    with results_file.open("w") as f:
        f.write(results.model_dump_json(indent=4))
