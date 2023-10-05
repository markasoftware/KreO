147a0 ??$addressof@U_Container_proxy@std@@@std@@YAPAU_Container_proxy@0@AAU10@@Z "struct std::_Container_proxy * __cdecl std::addressof<struct std::_Container_proxy>(struct std::_Container_proxy &)"

## why are these detected? all these functions don't take arguments probably because ecx register is storing some old value

    1ade0 ?set_app_type@__scrt_main_policy@@SAXXZ "public: static void __cdecl __scrt_main_policy::set_app_type(void)"
    17ad0 ?max@?$numeric_limits@H@std@@SAHXZ "public: static int __cdecl std::numeric_limits<int>::max(void)"
    1ae20 ?set_fmode@__scrt_file_policy@@SAXXZ "public: static void __cdecl __scrt_file_policy::set_fmode(void)"
    1c2a0 ___scrt_initialize_winrt "___scrt_initialize_winrt"
    1c380 ___scrt_stub_for_initialize_mta "___scrt_stub_for_initialize_mta"
    1a990 pre_c_initialization "pre_c_initialization"
    1c290 ___scrt_initialize_mta "___scrt_initialize_mta"
    1bf20 __get_startup_file_mode "__get_startup_file_mode"

## should be in the ground truth (belong to a union type):

    15440 ??0_Bxty@?$_String_val@U?$_Simple_types@D@std@@@std@@QAE@XZ "public: __thiscall std::_String_val<struct std::_Simple_types<char> >::_Bxty::_Bxty(void)"
    15ad0 ??1_Bxty@?$_String_val@U?$_Simple_types@D@std@@@std@@QAE@XZ "public: __thiscall std::_String_val<struct std::_Simple_types<char> >::_Bxty::~_Bxty(void)"

## called by vector allocator:

13f00 ??$_Get_size_of_n@$00@std@@YAII@Z "unsigned int __cdecl std::_Get_size_of_n<1>(unsigned int)"

## called by different vector allocator:

    13f20 ??$_Get_size_of_n@$07@std@@YAII@Z "unsigned int __cdecl std::_Get_size_of_n<8>(unsigned int)"

## These functions should not be detected, the ecx register when called contains a pointer to a constructed object that calls operator new (probably because ecx register stores old value)

    1a890 ??2@YAPAXI@Z "void * __cdecl operator new(unsigned int)"
    15c30 ??2@YAPAXIPAX@Z "void * __cdecl operator new(unsigned int,void *)"
    1a5a0 ??2_Fac_node@std@@SAPAXI@Z "public: static void * __cdecl std::_Fac_node::operator new(unsigned int)"


    

13990 ??$_Allocate@$07U_Default_allocate_traits@std@@$0A@@std@@YAPAXI@Z "void * __cdecl std::_Allocate<8,struct std::_Default_allocate_traits,0>(unsigned int)"

139e0 ??$_Allocate_manually_vector_aligned@U_Default_allocate_traits@std@@@std@@YAPAXI@Z "void * __cdecl std::_Allocate_manually_vector_aligned<struct std::_Default_allocate_traits>(unsigned int)"

148f0 ??$forward@AAPAV_Facet_base@std@@@std@@YAAAPAV_Facet_base@0@AAPAV10@@Z "class std::_Facet_base * & __cdecl std::forward<class std::_Facet_base * &>(class std::_Facet_base * &)"

14950 ??$forward@PAU_Container_base12@std@@@std@@YA$$QAPAU_Container_base12@0@AAPAU10@@Z "struct std::_Container_base12 * && __cdecl std::forward<struct std::_Container_base12 *>(struct std::_Container_base12 * &)"
1a680 ?_Facet_Register@std@@YAXPAV_Facet_base@1@@Z "void __cdecl std::_Facet_Register(class std::_Facet_base *)"
146e0 ??$_Voidify_iter@PAU_Container_proxy@std@@@std@@YAPAXPAU_Container_proxy@0@@Z "void * __cdecl std::_Voidify_iter<struct std::_Container_proxy *>(struct std::_Container_proxy *)"
14a10 ??$use_facet@V?$codecvt@DDU_Mbstatet@@@std@@@std@@YAABV?$codecvt@DDU_Mbstatet@@@0@ABVlocale@0@@Z "class std::codecvt<char,char,struct _Mbstatet> const & __cdecl std::use_facet<class std::codecvt<char,char,struct _Mbstatet> >(class std::locale const &)"

19b50 ??$_Convert_size@I@std@@YAII@Z "unsigned int __cdecl std::_Convert_size<unsigned int>(unsigned int)"


16250 ?_Allocate@_Default_allocate_traits@std@@SAPAXI@Z "public: static void * __cdecl std::_Default_allocate_traits::_Allocate(unsigned int)"

makes sense that these functions are detected:

19b70 ??$addressof@U_Container_base12@std@@@std@@YAPAU_Container_base12@0@AAU10@@Z "struct std::_Container_base12 * __cdecl std::addressof<struct std::_Container_base12>(struct std::_Container_base12 &)"
17af0 ?max_size@?$_Default_allocator_traits@V?$allocator@D@std@@@std@@SAIABV?$allocator@D@2@@Z "public: static unsigned int __cdecl std::_Default_allocator_traits<class std::allocator<char> >::max_size(class std::allocator<char> const &)"
17b10 ?max_size@?$_Default_allocator_traits@V?$allocator@E@std@@@std@@SAIABV?$allocator@E@2@@Z "public: static unsigned int __cdecl std::_Default_allocator_traits<class std::allocator<unsigned char> >::max_size(class std::allocator<unsigned char> const &)"
13b30 ??$_Construct_in_place@U_Container_proxy@std@@PAU_Container_base12@2@@std@@YAXAAU_Container_proxy@0@$$QAPAU_Container_base12@0@@Z "void __cdecl std::_Construct_in_place<struct std::_Container_proxy,struct std::_Container_base12 *>(struct std::_Container_proxy &,struct std::_Container_base12 * &&)"
1a030 ?length@?$_Narrow_char_traits@DH@std@@SAIQBD@Z "public: static unsigned int __cdecl std::_Narrow_char_traits<char,int>::length(char const * const)"
147c0 ??$addressof@V?$basic_filebuf@DU?$char_traits@D@std@@@std@@@std@@YAPAV?$basic_filebuf@DU?$char_traits@D@std@@@0@AAV10@@Z "class std::basic_filebuf<char,struct std::char_traits<char> > * __cdecl std::addressof<class std::basic_filebuf<char,struct std::char_traits<char> > >(class std::basic_filebuf<char,struct std::char_traits<char> > &)"
17c10 ?move@?$_Char_traits@DH@std@@SAPADQADQBDI@Z "public: static char * __cdecl std::_Char_traits<char,int>::move(char * const,char const * const,unsigned int)"
148c0 ??$exchange@PAV_Facet_base@std@@$$T@std@@YAPAV_Facet_base@0@AAPAV10@$$QA$$T@Z "class std::_Facet_base * __cdecl std::exchange<class std::_Facet_base *,std::nullptr_t>(class std::_Facet_base * &,std::nullptr_t &&)"

14890 ??$exchange@PAU_Container_proxy@std@@$$T@std@@YAPAU_Container_proxy@0@AAPAU10@$$QA$$T@Z "struct std::_Container_proxy * __cdecl std::exchange<struct std::_Container_proxy *,std::nullptr_t>(struct std::_Container_proxy * &,std::nullptr_t &&)"

## These are all called in same tarce when deallocating, in same trace as allocator constructor:

    13cb0 ??$_Deallocate_plain@V?$allocator@U_Container_proxy@std@@@std@@@std@@YAXAAV?$allocator@U_Container_proxy@std@@@0@QAU_Container_proxy@0@@Z "void __cdecl std::_Deallocate_plain<class std::allocator<struct std::_Container_proxy> >(class std::allocator<struct std::_Container_proxy> &,struct std::_Container_proxy * const)"
    17640 ?deallocate@?$_Default_allocator_traits@V?$allocator@U_Container_proxy@std@@@std@@@std@@SAXAAV?$allocator@U_Container_proxy@std@@@2@QAU_Container_proxy@2@I@Z "public: static void __cdecl std::_Default_allocator_traits<class std::allocator<struct std::_Container_proxy> >::deallocate(class std::allocator<struct std::_Container_proxy> &,struct std::_Container_proxy * const,unsigned int)"
    13d20 ??$_Delete_plain_internal@V?$allocator@U_Container_proxy@std@@@std@@@std@@YAXAAV?$allocator@U_Container_proxy@std@@@0@QAU_Container_proxy@0@@Z "void __cdecl std::_Delete_plain_internal<class std::allocator<struct std::_Container_proxy> >(class std::allocator<struct std::_Container_proxy> &,struct std::_Container_proxy * const)"