import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, Self, cast

import pdbparse  # pyright: ignore[reportMissingTypeStubs]
from construct import (  # pyright: ignore[reportMissingTypeStubs]
    Container,
    ListContainer,
)
from pdbparse.tpi import base_type
from pydantic import BaseModel, ConfigDict, Field

import postgame.analysis_results as ar

# section header information
# pdb.streams[pdb._stream_names["STREAM_SECT_HDR"]]

# global symbol information
# pdb.streams[pdb._stream_names["STREAM_GSYM"]]

# type data information
# pdb.streams[pdb._stream_names["STREAM_TPI"]]


class SectionHeaderInfo(BaseModel):
    name: str = Field(alias="Name")
    virtual_size: int = Field(alias="VirtualSize")
    virtual_addr: int = Field(alias="VirtualAddress")

    # model_config = ConfigDict(extra="ignore")

    @classmethod
    def model_validate_pdb(
        cls: type[Self],
        obj: dict[str, Any],
        *,
        strict: bool | None = None,
        from_attributes: bool | None = None,
        context: dict[str, Any] | None = None,
    ) -> Self:
        obj.update(dict(obj["Misc"]))

        return super().model_validate(
            obj, strict=strict, from_attributes=from_attributes, context=context
        )


class BaseType(BaseModel):
    length: int
    tpi_idx: int
    leaf_type: str | int


class TypeProp(BaseModel):
    fwdref: bool
    opcast: bool
    opassign: bool
    cnested: bool
    isnested: bool
    ovlops: bool
    ctor: bool
    packed: bool
    reserved: int
    scoped: bool


class FldAttr(BaseModel):
    noconstruct: bool
    noinherit: bool
    pseudo: bool
    mprop: str
    access: str
    compgenx: bool


class SubStruct(BaseModel):
    leaf_type: str | int
    name: str | None = None
    enum_value: int | None = None
    fldattr: FldAttr | None = None
    index: BaseType | str | None = None
    offset: int | None = None
    type_info: None = None


class FieldList(BaseType):
    substructs: ListContainer | None = None

    model_config = ConfigDict(arbitrary_types_allowed=True)


class TypeModifier(BaseModel):
    unaligned: bool
    volatile: bool
    const: bool


class UType(BaseType):
    count: int | None = None
    prop: TypeProp | None = None
    fieldlist: FieldList | str | None = None
    derived: str | None = None
    vshape: str | Container | None = None
    name: str | None = None
    size: int | None = None
    vt_descriptors: dict[str, Any] | None = None
    modified_type: str | None = None
    modifier: TypeModifier | None = None

    model_config = ConfigDict(arbitrary_types_allowed=True)


class PtrAttr(BaseModel):
    mode: str
    type: str
    restrict: bool
    unaligned: bool
    const: bool
    volatile: bool
    flat32: bool


class TypeData(BaseType):
    size: int | None = None
    count: int | None = None
    prop: TypeProp | None = None
    utype: Container | str | None = None
    substructs: ListContainer | None = None
    fieldlist: FieldList | str | None = None
    derived: str | None = None
    vshape: str | Container | None = None
    name: str | None = None
    ptr_attr: PtrAttr | None = None
    element_type: str | Container | None = None
    index_type: str | None = None
    modified_type: str | Container | None = None
    modifier: TypeModifier | None = None
    vt_descriptors: dict[str, Any] | None = None

    model_config = ConfigDict(arbitrary_types_allowed=True)


def recursive_container_to_dict(container: Container) -> Any:
    dic = cast("dict[str, Any]", dict(container))
    new_dic: dict[str, Any] = {}
    for k, v in dic.items():
        if k.startswith("_"):
            continue
        # elif isinstance(v, Container):
        #     v = recursive_container_to_dict(v)
        new_dic[k] = v

    return new_dic


class GlobalFunctionData(BaseModel):
    length: int
    leaf_type: int
    symtype: int
    offset: int
    segment: int
    name: str


def get_ea(function: GlobalFunctionData, sections: list[SectionHeaderInfo]) -> int:
    return sections[function.segment - 1].virtual_addr + function.offset


def struct_pretty_str(
    struct: TypeData,
    globals: dict[str, GlobalFunctionData],
    sections: list[SectionHeaderInfo],
) -> str:
    if not isinstance(struct.fieldlist, FieldList) or not struct.fieldlist.substructs:
        msg = "fieldlist doesn't exist...this shouldn't happen"
        raise RuntimeError(msg)

    ret = f"{struct.name}\n"
    members = cast("list[Container]", list(struct.fieldlist.substructs))
    for member in members:
        leaf_type = cast(
            "str", member.leaf_type  # pyright: ignore[reportUnknownMemberType]
        )
        if leaf_type == "LF_METHOD":
            ret += f"    {member.name}\n"  # pyright: ignore
        elif leaf_type == "LF_ONEMETHOD":
            # fld_attr = FldAttr.model_validate(dict(member.fldattr))

            # if (
            #     fld_attr.compgenx
            # ):  # remove compgenx functions (maybe we don't want to remove these from the gt)
            #     continue

            intro = ""
            if isinstance(member.intro, Container):
                intro = member.intro.str_data
            else:
                intro = member.intro

            ret += f"    {member.leaf_type} {intro}\n"  # pyright: ignore
    return ret


def type_has_methods(structure: TypeData) -> bool:
    if (
        not isinstance(structure.fieldlist, FieldList)
        or structure.fieldlist.substructs is None
    ):
        return False

    for substruct in cast("list[Container]", structure.fieldlist.substructs):
        leaf_type = cast(
            "str", substruct.leaf_type  # pyright: ignore[reportUnknownMemberType]
        )
        if leaf_type == "LF_ONEMETHOD" or leaf_type == "LF_METHOD":
            return True
    return False


def main(pdb_file: Path):
    pdb = pdbparse.parse(  # pyright: ignore[reportUnknownMemberType]
        "data/tinyxml2/tinyxml2.pdb"
    )

    sections_raw: list[Container] = cast(
        "list[Container]",
        pdb.streams[pdb._stream_names["STREAM_SECT_HDR"]].sections,  # pyright: ignore
    )

    sections: list[SectionHeaderInfo] = []
    for section in sections_raw:
        sec_dic: dict[str, Any] = dict(section)
        sections.append(SectionHeaderInfo.model_validate_pdb(sec_dic))

    globals_raw: dict[str, Container] = cast(
        "dict[str, Container]",
        pdb.streams[pdb._stream_names["STREAM_GSYM"]].funcs,  # pyright: ignore
    )

    globals: dict[str, GlobalFunctionData] = {}
    for glob in globals_raw.values():
        glob_dic = cast("dict[str, Any]", dict(glob))
        global_data = GlobalFunctionData.model_validate(glob_dic)
        globals[global_data.name] = global_data

    raw_types: dict[int, Container] = cast(
        "dict[int, Container]",
        pdb.streams[pdb._stream_names["STREAM_TPI"]].types,  # pyright: ignore
    )

    types: list[TypeData] = []
    recursive_container_to_dict(list(raw_types.values())[30])
    for typ in raw_types.values():
        types.append(TypeData.model_validate(recursive_container_to_dict(typ)))

    structures: list[TypeData] = []
    for typ in types:
        if (
            (typ.leaf_type == "LF_CLASS" or typ.leaf_type == "LF_STRUCTURE")
            and typ.prop
            and not typ.prop.fwdref
            and type_has_methods(typ)
        ):
            structures.append(typ)

    print("*******  Structures  *******")
    for struct in structures:
        print(struct_pretty_str(struct, globals, sections) + "\n")
