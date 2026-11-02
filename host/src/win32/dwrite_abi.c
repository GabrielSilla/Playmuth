#include "DwriteAbi.h"
void hostGateNamed(const char*);
static __attribute__((ms_abi)) int32_t ms_Query(void* s,const void* iid,void** out){hostGateNamed("DWrite::Query");return dw_Query(s,iid,out);}
static __attribute__((ms_abi)) uint32_t ms_AddRef(void* s){hostGateNamed("DWrite::AddRef");return dw_AddRef(s);}
static __attribute__((ms_abi)) uint32_t ms_Release(void* s){hostGateNamed("DWrite::Release");return dw_Release(s);}
static __attribute__((ms_abi)) int32_t ms_CreateBuilder(void* s,void** out){hostGateNamed("DWrite::CreateBuilder");return dw_CreateBuilder(s,out);}
static __attribute__((ms_abi)) int32_t ms_CreateFontSet(void* s,void** out){hostGateNamed("DWrite::CreateFontSet");return dw_CreateFontSet(s,out);}
static __attribute__((ms_abi)) int32_t ms_CreateSystemFallback(void* s,void** out){hostGateNamed("DWrite::CreateSystemFallback");return dw_CreateSystemFallback(s,out);}
static __attribute__((ms_abi)) int32_t ms_CreateFallbackBuilder(void* s,void** out){hostGateNamed("DWrite::CreateFallbackBuilder");return dw_CreateFallbackBuilder(s,out);}
static __attribute__((ms_abi)) int32_t ms_CreateTypography(void* s,void** out){hostGateNamed("DWrite::CreateTypography");return dw_CreateTypography(s,out);}
static __attribute__((ms_abi)) int32_t ms_FontFile(void* s,const uint16_t* path,const void* time,void** out){hostGateNamed("DWrite::FontFile");return dw_FontFile(s,path,time,out);}
static __attribute__((ms_abi)) int32_t ms_AddFile(void* s,void* file){hostGateNamed("DWrite::AddFile");return dw_AddFile(s,file);}
static __attribute__((ms_abi)) int32_t ms_AddSet(void* s,void* set){hostGateNamed("DWrite::AddSet");return dw_AddSet(s,set);}
static __attribute__((ms_abi)) int32_t ms_Collection(void* s,void* set,void** out){hostGateNamed("DWrite::Collection");return dw_Collection(s,set,out);}
static __attribute__((ms_abi)) uint32_t ms_FamilyCount(void* s){hostGateNamed("DWrite::FamilyCount");return dw_FamilyCount(s);}
static __attribute__((ms_abi)) int32_t ms_Family(void* s,uint32_t index,void** out){hostGateNamed("DWrite::Family");return dw_Family(s,index,out);}
static __attribute__((ms_abi)) int32_t ms_FindFamily(void* s,const uint16_t* name,uint32_t* index,int32_t* exists){hostGateNamed("DWrite::FindFamily");return dw_FindFamily(s,name,index,exists);}
static __attribute__((ms_abi)) uint32_t ms_FontCount(void* s){hostGateNamed("DWrite::FontCount");return dw_FontCount(s);}
static __attribute__((ms_abi)) int32_t ms_FontAt(void* s,uint32_t index,void** out){hostGateNamed("DWrite::FontAt");return dw_FontAt(s,index,out);}
static __attribute__((ms_abi)) int32_t ms_MatchingFont(void* s,uint32_t weight,uint32_t stretch,uint32_t style,void** out){hostGateNamed("DWrite::MatchingFont");return dw_MatchingFont(s,weight,stretch,style,out);}
static __attribute__((ms_abi)) int32_t ms_FamilyNames(void* s,void** out){hostGateNamed("DWrite::FamilyNames");return dw_FamilyNames(s,out);}
static __attribute__((ms_abi)) int32_t ms_FontFamily(void* s,void** out){hostGateNamed("DWrite::FontFamily");return dw_FontFamily(s,out);}
static __attribute__((ms_abi)) int32_t ms_Face(void* s,void** out){hostGateNamed("DWrite::Face");return dw_Face(s,out);}
static __attribute__((ms_abi)) uint32_t ms_FontWeight(void* s){hostGateNamed("DWrite::FontWeight");return dw_FontWeight(s);}
static __attribute__((ms_abi)) uint32_t ms_FontStyle(void* s){hostGateNamed("DWrite::FontStyle");return dw_FontStyle(s);}
static __attribute__((ms_abi)) uint32_t ms_FontStretch(void* s){hostGateNamed("DWrite::FontStretch");return dw_FontStretch(s);}
static __attribute__((ms_abi)) uint32_t ms_Zero(void* s){hostGateNamed("DWrite::Zero");return dw_Zero(s);}
static __attribute__((ms_abi)) void ms_FontMetrics(void* s,void* out){hostGateNamed("DWrite::FontMetrics");dw_FontMetrics(s,out);}
static __attribute__((ms_abi)) int32_t ms_HasChar(void* s,uint32_t value,int32_t* exists){hostGateNamed("DWrite::HasChar");return dw_HasChar(s,value,exists);}
static __attribute__((ms_abi)) uint16_t ms_GlyphCount(void* s){hostGateNamed("DWrite::GlyphCount");return dw_GlyphCount(s);}
static __attribute__((ms_abi)) int32_t ms_GlyphIndices(void* s,const uint32_t* chars,uint32_t count,uint16_t* out){hostGateNamed("DWrite::GlyphIndices");return dw_GlyphIndices(s,chars,count,out);}
static __attribute__((ms_abi)) int32_t ms_GlyphMetrics(void* s,const uint16_t* glyphs,uint32_t count,void* out,int32_t sideways){hostGateNamed("DWrite::GlyphMetrics");return dw_GlyphMetrics(s,glyphs,count,out,sideways);}
static __attribute__((ms_abi)) uint32_t ms_FaceType(void* s){hostGateNamed("DWrite::FaceType");return dw_FaceType(s);}
static __attribute__((ms_abi)) int32_t ms_AddMappings(void* s,void* fallback){hostGateNamed("DWrite::AddMappings");return dw_AddMappings(s,fallback);}
static __attribute__((ms_abi)) int32_t ms_AddMapping(void* s,const void* ranges,uint32_t rangeCount,const uint16_t* const* families,uint32_t familyCount,void* collection,const uint16_t* locale,const uint16_t* base,float scale){hostGateNamed("DWrite::AddMapping");return dw_AddMapping(s,ranges,rangeCount,families,familyCount,collection,locale,base,scale);}
static __attribute__((ms_abi)) int32_t ms_Format(void* s,const uint16_t* family,void* collection,uint32_t weight,uint32_t style,uint32_t stretch,float size,const uint16_t* locale,void** out){hostGateNamed("DWrite::Format");return dw_Format(s,family,collection,weight,style,stretch,size,locale,out);}
static __attribute__((ms_abi)) int32_t ms_Layout(void* s,const uint16_t* text,uint32_t count,void* format,float width,float height,void** out){hostGateNamed("DWrite::Layout");return dw_Layout(s,text,count,format,width,height,out);}
static __attribute__((ms_abi)) int32_t ms_SetTextAlign(void* s,uint32_t value){hostGateNamed("DWrite::SetTextAlign");return dw_SetTextAlign(s,value);}
static __attribute__((ms_abi)) uint32_t ms_GetTextAlign(void* s){hostGateNamed("DWrite::GetTextAlign");return dw_GetTextAlign(s);}
static __attribute__((ms_abi)) int32_t ms_SetParagraphAlign(void* s,uint32_t value){hostGateNamed("DWrite::SetParagraphAlign");return dw_SetParagraphAlign(s,value);}
static __attribute__((ms_abi)) uint32_t ms_GetParagraphAlign(void* s){hostGateNamed("DWrite::GetParagraphAlign");return dw_GetParagraphAlign(s);}
static __attribute__((ms_abi)) int32_t ms_SetWrap(void* s,uint32_t value){hostGateNamed("DWrite::SetWrap");return dw_SetWrap(s,value);}
static __attribute__((ms_abi)) uint32_t ms_GetWrap(void* s){hostGateNamed("DWrite::GetWrap");return dw_GetWrap(s);}
static __attribute__((ms_abi)) int32_t ms_SetReading(void* s,uint32_t value){hostGateNamed("DWrite::SetReading");return dw_SetReading(s,value);}
static __attribute__((ms_abi)) uint32_t ms_GetReading(void* s){hostGateNamed("DWrite::GetReading");return dw_GetReading(s);}
static __attribute__((ms_abi)) int32_t ms_SetFlow(void* s,uint32_t value){hostGateNamed("DWrite::SetFlow");return dw_SetFlow(s,value);}
static __attribute__((ms_abi)) uint32_t ms_GetFlow(void* s){hostGateNamed("DWrite::GetFlow");return dw_GetFlow(s);}
static __attribute__((ms_abi)) int32_t ms_SetLineSpacing(void* s,uint32_t method,float spacing,float baseline){hostGateNamed("DWrite::SetLineSpacing");return dw_SetLineSpacing(s,method,spacing,baseline);}
static __attribute__((ms_abi)) int32_t ms_GetLineSpacing(void* s,uint32_t* method,float* spacing,float* baseline){hostGateNamed("DWrite::GetLineSpacing");return dw_GetLineSpacing(s,method,spacing,baseline);}
static __attribute__((ms_abi)) int32_t ms_SetTabStop(void* s,float value){hostGateNamed("DWrite::SetTabStop");return dw_SetTabStop(s,value);}
static __attribute__((ms_abi)) float ms_GetTabStop(void* s){hostGateNamed("DWrite::GetTabStop");return dw_GetTabStop(s);}
static __attribute__((ms_abi)) int32_t ms_SetTrimming(void* s,const void* options,void* sign){hostGateNamed("DWrite::SetTrimming");return dw_SetTrimming(s,options,sign);}
static __attribute__((ms_abi)) int32_t ms_GetTrimming(void* s,void* options,void** sign){hostGateNamed("DWrite::GetTrimming");return dw_GetTrimming(s,options,sign);}
// D2D1_POINT_2F arrives by value in an integer register: x low, y high.
static void dwPoint(uint64_t p,float* x,float* y){uint32_t a=(uint32_t)p,b=(uint32_t)(p>>32);__builtin_memcpy(x,&a,4);__builtin_memcpy(y,&b,4);}
static __attribute__((ms_abi)) int32_t ms_GlyphOrigins41(void* s,const void* run,uint64_t origin,float* origins){(void)s;hostGateNamed("DWrite::GlyphOrigins");float x,y;dwPoint(origin,&x,&y);return dw_GlyphOrigins(run,x,y,origins);}
static __attribute__((ms_abi)) int32_t ms_GlyphOrigins42(void* s,const void* run,uint32_t mode,uint64_t origin,const void* transform,float* origins){(void)s;(void)mode;(void)transform;hostGateNamed("DWrite::GlyphOrigins");float x,y;dwPoint(origin,&x,&y);return dw_GlyphOrigins(run,x,y,origins);}
static __attribute__((ms_abi)) int32_t ms_MinWidth(void* s,float* out){hostGateNamed("DWrite::MinWidth");return dw_MinWidth(s,out);}
static __attribute__((ms_abi)) int32_t ms_SetLocale(void* s,const uint16_t* value,uint64_t range){hostGateNamed("DWrite::SetLocale");return dw_SetLocale(s,value,range);}
static __attribute__((ms_abi)) int32_t ms_EllipsisSign(void* s,void* format,void** out){hostGateNamed("DWrite::EllipsisSign");return dw_EllipsisSign(s,format,out);}
static __attribute__((ms_abi)) int32_t ms_SignDraw(void* s){hostGateNamed("DWrite::SignDraw");return dw_SignDraw(s);}
static __attribute__((ms_abi)) int32_t ms_SignMetrics(void* s,void* out){hostGateNamed("DWrite::SignMetrics");return dw_SignMetrics(s,out);}
static __attribute__((ms_abi)) int32_t ms_SignOverhang(void* s,void* out){hostGateNamed("DWrite::SignOverhang");return dw_SignOverhang(s,out);}
static __attribute__((ms_abi)) int32_t ms_SignBreaks(void* s,uint32_t* before,uint32_t* after){hostGateNamed("DWrite::SignBreaks");return dw_SignBreaks(s,before,after);}
static __attribute__((ms_abi)) int32_t ms_SetFallback(void* s,void* value){hostGateNamed("DWrite::SetFallback");return dw_SetFallback(s,value);}
static __attribute__((ms_abi)) int32_t ms_GetFallback(void* s,void** out){hostGateNamed("DWrite::GetFallback");return dw_GetFallback(s,out);}
static __attribute__((ms_abi)) int32_t ms_SetFontWeight(void* s,uint32_t value,uint64_t range){hostGateNamed("DWrite::SetFontWeight");return dw_SetFontWeight(s,value,range);}
static __attribute__((ms_abi)) int32_t ms_SetFontStyle(void* s,uint32_t value,uint64_t range){hostGateNamed("DWrite::SetFontStyle");return dw_SetFontStyle(s,value,range);}
static __attribute__((ms_abi)) int32_t ms_SetFontSize(void* s,float value,uint64_t range){hostGateNamed("DWrite::SetFontSize");return dw_SetFontSize(s,value,range);}
static __attribute__((ms_abi)) float ms_GetFontSize(void* s){hostGateNamed("DWrite::GetFontSize");return dw_GetFontSize(s);}
static __attribute__((ms_abi)) int32_t ms_SetFamily(void* s,const uint16_t* name,uint64_t range){hostGateNamed("DWrite::SetFamily");return dw_SetFamily(s,name,range);}
static __attribute__((ms_abi)) int32_t ms_SetEffect(void* s,void* value,uint64_t range){hostGateNamed("DWrite::SetEffect");return dw_SetEffect(s,value,range);}
static __attribute__((ms_abi)) int32_t ms_SetTypography(void* s,void* value,uint64_t range){hostGateNamed("DWrite::SetTypography");return dw_SetTypography(s,value,range);}
static __attribute__((ms_abi)) int32_t ms_Feature(void* s,uint64_t feature){hostGateNamed("DWrite::Feature");return dw_Feature(s,feature);}
static __attribute__((ms_abi)) int32_t ms_Draw(void* s,void* context,void* renderer,float x,float y){hostGateNamed("DWrite::Draw");return dw_Draw(s,context,renderer,x,y);}
static __attribute__((ms_abi)) int32_t ms_TextMetrics(void* s,void* out){hostGateNamed("DWrite::TextMetrics");return dw_TextMetrics(s,out);}
static __attribute__((ms_abi)) int32_t ms_Overhang(void* s,void* out){hostGateNamed("DWrite::Overhang");return dw_Overhang(s,out);}
static __attribute__((ms_abi)) int32_t ms_LineMetrics(void* s,void* out,uint32_t capacity,uint32_t* actual){hostGateNamed("DWrite::LineMetrics");return dw_LineMetrics(s,out,capacity,actual);}
static __attribute__((ms_abi)) int32_t ms_ClusterMetrics(void* s,void* out,uint32_t capacity,uint32_t* actual){hostGateNamed("DWrite::ClusterMetrics");return dw_ClusterMetrics(s,out,capacity,actual);}
static __attribute__((ms_abi)) int32_t ms_SetWidth(void* s,float value){hostGateNamed("DWrite::SetWidth");return dw_SetWidth(s,value);}
static __attribute__((ms_abi)) float ms_GetWidth(void* s){hostGateNamed("DWrite::GetWidth");return dw_GetWidth(s);}
static __attribute__((ms_abi)) int32_t ms_SetHeight(void* s,float value){hostGateNamed("DWrite::SetHeight");return dw_SetHeight(s,value);}
static __attribute__((ms_abi)) float ms_GetHeight(void* s){hostGateNamed("DWrite::GetHeight");return dw_GetHeight(s);}
static __attribute__((ms_abi)) int32_t ms_PairKerning(void* s,int32_t enabled,uint64_t range){hostGateNamed("DWrite::PairKerning");return dw_PairKerning(s,enabled,range);}
static __attribute__((ms_abi)) int32_t ms_Spacing(void* s,float leading,float trailing,float minimum,uint64_t range){hostGateNamed("DWrite::Spacing");return dw_Spacing(s,leading,trailing,minimum,range);}
static __attribute__((ms_abi)) int32_t ms_Analysis(void* s,const void* run,float ppd,const void* matrix,uint32_t rendering,uint32_t measuring,float x,float y,void** out){hostGateNamed("DWrite::Analysis");return dw_Analysis(s,run,ppd,matrix,rendering,measuring,x,y,out);}
static __attribute__((ms_abi)) int32_t ms_Analysis2(void* s,const void* run,const void* matrix,uint32_t rendering,uint32_t measuring,uint32_t grid,uint32_t antialias,float x,float y,void** out){hostGateNamed("DWrite::Analysis2");return dw_Analysis2(s,run,matrix,rendering,measuring,grid,antialias,x,y,out);}
static __attribute__((ms_abi)) int32_t ms_TextureBounds(void* s,uint32_t type,int32_t* rect){hostGateNamed("DWrite::TextureBounds");return dw_TextureBounds(s,type,rect);}
static __attribute__((ms_abi)) int32_t ms_Texture(void* s,uint32_t type,const int32_t* rect,uint8_t* bytes,uint32_t size){hostGateNamed("DWrite::Texture");return dw_Texture(s,type,rect,bytes,size);}
static __attribute__((ms_abi)) uint32_t ms_StringCount(void* s){hostGateNamed("DWrite::StringCount");return dw_StringCount(s);}
static __attribute__((ms_abi)) int32_t ms_FindLocale(void* s,const uint16_t* locale,uint32_t* index,int32_t* exists){hostGateNamed("DWrite::FindLocale");return dw_FindLocale(s,locale,index,exists);}
static __attribute__((ms_abi)) int32_t ms_LocaleLength(void* s,uint32_t index,uint32_t* size){hostGateNamed("DWrite::LocaleLength");return dw_LocaleLength(s,index,size);}
static __attribute__((ms_abi)) int32_t ms_Locale(void* s,uint32_t index,uint16_t* text,uint32_t size){hostGateNamed("DWrite::Locale");return dw_Locale(s,index,text,size);}
static __attribute__((ms_abi)) int32_t ms_StringLength(void* s,uint32_t index,uint32_t* size){hostGateNamed("DWrite::StringLength");return dw_StringLength(s,index,size);}
static __attribute__((ms_abi)) int32_t ms_String(void* s,uint32_t index,uint16_t* text,uint32_t size){hostGateNamed("DWrite::String");return dw_String(s,index,text,size);}
static __attribute__((ms_abi)) int32_t ms_Factory_3(void* self){(void)self;return dw_Missing("Factory",3);}
static __attribute__((ms_abi)) int32_t ms_Factory_4(void* self){(void)self;return dw_Missing("Factory",4);}
static __attribute__((ms_abi)) int32_t ms_Factory_5(void* self){(void)self;return dw_Missing("Factory",5);}
static __attribute__((ms_abi)) int32_t ms_Factory_6(void* self){(void)self;return dw_Missing("Factory",6);}
static __attribute__((ms_abi)) int32_t ms_Factory_8(void* self){(void)self;return dw_Missing("Factory",8);}
static __attribute__((ms_abi)) int32_t ms_Factory_9(void* self){(void)self;return dw_Missing("Factory",9);}
static __attribute__((ms_abi)) int32_t ms_Factory_10(void* self){(void)self;return dw_Missing("Factory",10);}
static __attribute__((ms_abi)) int32_t ms_Factory_11(void* self){(void)self;return dw_Missing("Factory",11);}
static __attribute__((ms_abi)) int32_t ms_Factory_12(void* self){(void)self;return dw_Missing("Factory",12);}
static __attribute__((ms_abi)) int32_t ms_Factory_13(void* self){(void)self;return dw_Missing("Factory",13);}
static __attribute__((ms_abi)) int32_t ms_Factory_14(void* self){(void)self;return dw_Missing("Factory",14);}
static __attribute__((ms_abi)) int32_t ms_Factory_17(void* self){(void)self;return dw_Missing("Factory",17);}
static __attribute__((ms_abi)) int32_t ms_Factory_19(void* self){(void)self;return dw_Missing("Factory",19);}
static __attribute__((ms_abi)) int32_t ms_Factory_21(void* self){(void)self;return dw_Missing("Factory",21);}
static __attribute__((ms_abi)) int32_t ms_Factory_22(void* self){(void)self;return dw_Missing("Factory",22);}
static __attribute__((ms_abi)) int32_t ms_Factory_24(void* self){(void)self;return dw_Missing("Factory",24);}
static __attribute__((ms_abi)) int32_t ms_Factory_25(void* self){(void)self;return dw_Missing("Factory",25);}
static __attribute__((ms_abi)) int32_t ms_Factory_28(void* self){(void)self;return dw_Missing("Factory",28);}
static __attribute__((ms_abi)) int32_t ms_Factory_29(void* self){(void)self;return dw_Missing("Factory",29);}
static __attribute__((ms_abi)) int32_t ms_Factory_32(void* self){(void)self;return dw_Missing("Factory",32);}
static __attribute__((ms_abi)) int32_t ms_Factory_33(void* self){(void)self;return dw_Missing("Factory",33);}
static __attribute__((ms_abi)) int32_t ms_Factory_34(void* self){(void)self;return dw_Missing("Factory",34);}
static __attribute__((ms_abi)) int32_t ms_Factory_35(void* self){(void)self;return dw_Missing("Factory",35);}
static __attribute__((ms_abi)) int32_t ms_Factory_38(void* self){(void)self;return dw_Missing("Factory",38);}
static __attribute__((ms_abi)) int32_t ms_Factory_39(void* self){(void)self;return dw_Missing("Factory",39);}
static __attribute__((ms_abi)) int32_t ms_Factory_40(void* self){(void)self;return dw_Missing("Factory",40);}
static __attribute__((ms_abi)) int32_t ms_Factory_44(void* self){(void)self;return dw_Missing("Factory",44);}
static __attribute__((ms_abi)) int32_t ms_Factory_45(void* self){(void)self;return dw_Missing("Factory",45);}
static __attribute__((ms_abi)) int32_t ms_Factory_46(void* self){(void)self;return dw_Missing("Factory",46);}
static __attribute__((ms_abi)) int32_t ms_Factory_47(void* self){(void)self;return dw_Missing("Factory",47);}
const void* const dwFactoryVtable[48]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_Factory_3,(const void*)&ms_Factory_4,(const void*)&ms_Factory_5,(const void*)&ms_Factory_6,(const void*)&ms_FontFile,(const void*)&ms_Factory_8,(const void*)&ms_Factory_9,(const void*)&ms_Factory_10,(const void*)&ms_Factory_11,(const void*)&ms_Factory_12,(const void*)&ms_Factory_13,(const void*)&ms_Factory_14,(const void*)&ms_Format,(const void*)&ms_CreateTypography,(const void*)&ms_Factory_17,(const void*)&ms_Layout,(const void*)&ms_Factory_19,(const void*)&ms_EllipsisSign,(const void*)&ms_Factory_21,(const void*)&ms_Factory_22,(const void*)&ms_Analysis,(const void*)&ms_Factory_24,(const void*)&ms_Factory_25,(const void*)&ms_CreateSystemFallback,(const void*)&ms_CreateFallbackBuilder,(const void*)&ms_Factory_28,(const void*)&ms_Factory_29,(const void*)&ms_Analysis2,(const void*)&ms_Analysis2,(const void*)&ms_Factory_32,(const void*)&ms_Factory_33,(const void*)&ms_Factory_34,(const void*)&ms_Factory_35,(const void*)&ms_CreateBuilder,(const void*)&ms_Collection,(const void*)&ms_Factory_38,(const void*)&ms_Factory_39,(const void*)&ms_Factory_40,(const void*)&ms_GlyphOrigins41,(const void*)&ms_GlyphOrigins42,(const void*)&ms_CreateBuilder,(const void*)&ms_Factory_44,(const void*)&ms_Factory_45,(const void*)&ms_Factory_46,(const void*)&ms_Factory_47};
static __attribute__((ms_abi)) int32_t ms_File_3(void* self){(void)self;return dw_Missing("File",3);}
static __attribute__((ms_abi)) int32_t ms_File_4(void* self){(void)self;return dw_Missing("File",4);}
static __attribute__((ms_abi)) int32_t ms_File_5(void* self){(void)self;return dw_Missing("File",5);}
const void* const dwFileVtable[6]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_File_3,(const void*)&ms_File_4,(const void*)&ms_File_5};
static __attribute__((ms_abi)) int32_t ms_Builder_3(void* self){(void)self;return dw_Missing("Builder",3);}
static __attribute__((ms_abi)) int32_t ms_Builder_4(void* self){(void)self;return dw_Missing("Builder",4);}
const void* const dwBuilderVtable[8]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_Builder_3,(const void*)&ms_Builder_4,(const void*)&ms_AddSet,(const void*)&ms_CreateFontSet,(const void*)&ms_AddFile};
static __attribute__((ms_abi)) int32_t ms_FontSet_4(void* self){(void)self;return dw_Missing("FontSet",4);}
static __attribute__((ms_abi)) int32_t ms_FontSet_5(void* self){(void)self;return dw_Missing("FontSet",5);}
static __attribute__((ms_abi)) int32_t ms_FontSet_6(void* self){(void)self;return dw_Missing("FontSet",6);}
static __attribute__((ms_abi)) int32_t ms_FontSet_7(void* self){(void)self;return dw_Missing("FontSet",7);}
static __attribute__((ms_abi)) int32_t ms_FontSet_8(void* self){(void)self;return dw_Missing("FontSet",8);}
static __attribute__((ms_abi)) int32_t ms_FontSet_9(void* self){(void)self;return dw_Missing("FontSet",9);}
static __attribute__((ms_abi)) int32_t ms_FontSet_10(void* self){(void)self;return dw_Missing("FontSet",10);}
static __attribute__((ms_abi)) int32_t ms_FontSet_11(void* self){(void)self;return dw_Missing("FontSet",11);}
static __attribute__((ms_abi)) int32_t ms_FontSet_12(void* self){(void)self;return dw_Missing("FontSet",12);}
const void* const dwFontSetVtable[13]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_FontCount,(const void*)&ms_FontSet_4,(const void*)&ms_FontSet_5,(const void*)&ms_FontSet_6,(const void*)&ms_FontSet_7,(const void*)&ms_FontSet_8,(const void*)&ms_FontSet_9,(const void*)&ms_FontSet_10,(const void*)&ms_FontSet_11,(const void*)&ms_FontSet_12};
static __attribute__((ms_abi)) int32_t ms_Collection_6(void* self){(void)self;return dw_Missing("Collection",6);}
const void* const dwCollectionVtable[9]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_FamilyCount,(const void*)&ms_Family,(const void*)&ms_FindFamily,(const void*)&ms_Collection_6,(const void*)&ms_CreateFontSet,(const void*)&ms_Family};
static __attribute__((ms_abi)) int32_t ms_Family_3(void* self){(void)self;return dw_Missing("Family",3);}
static __attribute__((ms_abi)) int32_t ms_Family_8(void* self){(void)self;return dw_Missing("Family",8);}
static __attribute__((ms_abi)) int32_t ms_Family_9(void* self){(void)self;return dw_Missing("Family",9);}
static __attribute__((ms_abi)) int32_t ms_Family_10(void* self){(void)self;return dw_Missing("Family",10);}
const void* const dwFamilyVtable[11]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_Family_3,(const void*)&ms_FontCount,(const void*)&ms_FontAt,(const void*)&ms_FamilyNames,(const void*)&ms_MatchingFont,(const void*)&ms_Family_8,(const void*)&ms_Family_9,(const void*)&ms_Family_10};
static __attribute__((ms_abi)) int32_t ms_Font_8(void* self){(void)self;return dw_Missing("Font",8);}
static __attribute__((ms_abi)) int32_t ms_Font_9(void* self){(void)self;return dw_Missing("Font",9);}
const void* const dwFontVtable[14]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_FontFamily,(const void*)&ms_FontWeight,(const void*)&ms_FontStretch,(const void*)&ms_FontStyle,(const void*)&ms_Zero,(const void*)&ms_Font_8,(const void*)&ms_Font_9,(const void*)&ms_Zero,(const void*)&ms_FontMetrics,(const void*)&ms_HasChar,(const void*)&ms_Face};
static __attribute__((ms_abi)) int32_t ms_Face_4(void* self){(void)self;return dw_Missing("Face",4);}
static __attribute__((ms_abi)) int32_t ms_Face_12(void* self){(void)self;return dw_Missing("Face",12);}
static __attribute__((ms_abi)) int32_t ms_Face_13(void* self){(void)self;return dw_Missing("Face",13);}
static __attribute__((ms_abi)) int32_t ms_Face_14(void* self){(void)self;return dw_Missing("Face",14);}
static __attribute__((ms_abi)) int32_t ms_Face_15(void* self){(void)self;return dw_Missing("Face",15);}
static __attribute__((ms_abi)) int32_t ms_Face_16(void* self){(void)self;return dw_Missing("Face",16);}
static __attribute__((ms_abi)) int32_t ms_Face_17(void* self){(void)self;return dw_Missing("Face",17);}
const void* const dwFaceVtable[18]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_FaceType,(const void*)&ms_Face_4,(const void*)&ms_Zero,(const void*)&ms_Zero,(const void*)&ms_Zero,(const void*)&ms_FontMetrics,(const void*)&ms_GlyphCount,(const void*)&ms_GlyphMetrics,(const void*)&ms_GlyphIndices,(const void*)&ms_Face_12,(const void*)&ms_Face_13,(const void*)&ms_Face_14,(const void*)&ms_Face_15,(const void*)&ms_Face_16,(const void*)&ms_Face_17};
const void* const dwFallbackBuilderVtable[6]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_AddMapping,(const void*)&ms_AddMappings,(const void*)&ms_CreateSystemFallback};
static __attribute__((ms_abi)) int32_t ms_Fallback_3(void* self){(void)self;return dw_Missing("Fallback",3);}
const void* const dwFallbackVtable[4]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_Fallback_3};
static __attribute__((ms_abi)) int32_t ms_Format_19(void* self){(void)self;return dw_Missing("Format",19);}
static __attribute__((ms_abi)) int32_t ms_Format_20(void* self){(void)self;return dw_Missing("Format",20);}
static __attribute__((ms_abi)) int32_t ms_Format_21(void* self){(void)self;return dw_Missing("Format",21);}
static __attribute__((ms_abi)) int32_t ms_Format_26(void* self){(void)self;return dw_Missing("Format",26);}
static __attribute__((ms_abi)) int32_t ms_Format_27(void* self){(void)self;return dw_Missing("Format",27);}
static __attribute__((ms_abi)) int32_t ms_Format_28(void* self){(void)self;return dw_Missing("Format",28);}
static __attribute__((ms_abi)) int32_t ms_Format_29(void* self){(void)self;return dw_Missing("Format",29);}
static __attribute__((ms_abi)) int32_t ms_Format_30(void* self){(void)self;return dw_Missing("Format",30);}
static __attribute__((ms_abi)) int32_t ms_Format_31(void* self){(void)self;return dw_Missing("Format",31);}
static __attribute__((ms_abi)) int32_t ms_Format_32(void* self){(void)self;return dw_Missing("Format",32);}
static __attribute__((ms_abi)) int32_t ms_Format_33(void* self){(void)self;return dw_Missing("Format",33);}
const void* const dwFormatVtable[36]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_SetTextAlign,(const void*)&ms_SetParagraphAlign,(const void*)&ms_SetWrap,(const void*)&ms_SetReading,(const void*)&ms_SetFlow,(const void*)&ms_SetTabStop,(const void*)&ms_SetTrimming,(const void*)&ms_SetLineSpacing,(const void*)&ms_GetTextAlign,(const void*)&ms_GetParagraphAlign,(const void*)&ms_GetWrap,(const void*)&ms_GetReading,(const void*)&ms_GetFlow,(const void*)&ms_GetTabStop,(const void*)&ms_GetTrimming,(const void*)&ms_GetLineSpacing,(const void*)&ms_Format_19,(const void*)&ms_Format_20,(const void*)&ms_Format_21,(const void*)&ms_FontWeight,(const void*)&ms_FontStyle,(const void*)&ms_FontStretch,(const void*)&ms_GetFontSize,(const void*)&ms_Format_26,(const void*)&ms_Format_27,(const void*)&ms_Format_28,(const void*)&ms_Format_29,(const void*)&ms_Format_30,(const void*)&ms_Format_31,(const void*)&ms_Format_32,(const void*)&ms_Format_33,(const void*)&ms_SetFallback,(const void*)&ms_GetFallback};
static __attribute__((ms_abi)) int32_t ms_Layout_19(void* self){(void)self;return dw_Missing("Layout",19);}
static __attribute__((ms_abi)) int32_t ms_Layout_20(void* self){(void)self;return dw_Missing("Layout",20);}
static __attribute__((ms_abi)) int32_t ms_Layout_21(void* self){(void)self;return dw_Missing("Layout",21);}
static __attribute__((ms_abi)) int32_t ms_Layout_22(void* self){(void)self;return dw_Missing("Layout",22);}
static __attribute__((ms_abi)) int32_t ms_Layout_23(void* self){(void)self;return dw_Missing("Layout",23);}
static __attribute__((ms_abi)) int32_t ms_Layout_24(void* self){(void)self;return dw_Missing("Layout",24);}
static __attribute__((ms_abi)) int32_t ms_Layout_25(void* self){(void)self;return dw_Missing("Layout",25);}
static __attribute__((ms_abi)) int32_t ms_Layout_26(void* self){(void)self;return dw_Missing("Layout",26);}
static __attribute__((ms_abi)) int32_t ms_Layout_27(void* self){(void)self;return dw_Missing("Layout",27);}
static __attribute__((ms_abi)) int32_t ms_Layout_30(void* self){(void)self;return dw_Missing("Layout",30);}
static __attribute__((ms_abi)) int32_t ms_Layout_34(void* self){(void)self;return dw_Missing("Layout",34);}
static __attribute__((ms_abi)) int32_t ms_Layout_36(void* self){(void)self;return dw_Missing("Layout",36);}
static __attribute__((ms_abi)) int32_t ms_Layout_37(void* self){(void)self;return dw_Missing("Layout",37);}
static __attribute__((ms_abi)) int32_t ms_Layout_39(void* self){(void)self;return dw_Missing("Layout",39);}
static __attribute__((ms_abi)) int32_t ms_Layout_44(void* self){(void)self;return dw_Missing("Layout",44);}
static __attribute__((ms_abi)) int32_t ms_Layout_45(void* self){(void)self;return dw_Missing("Layout",45);}
static __attribute__((ms_abi)) int32_t ms_Layout_46(void* self){(void)self;return dw_Missing("Layout",46);}
static __attribute__((ms_abi)) int32_t ms_Layout_47(void* self){(void)self;return dw_Missing("Layout",47);}
static __attribute__((ms_abi)) int32_t ms_Layout_48(void* self){(void)self;return dw_Missing("Layout",48);}
static __attribute__((ms_abi)) int32_t ms_Layout_49(void* self){(void)self;return dw_Missing("Layout",49);}
static __attribute__((ms_abi)) int32_t ms_Layout_50(void* self){(void)self;return dw_Missing("Layout",50);}
static __attribute__((ms_abi)) int32_t ms_Layout_51(void* self){(void)self;return dw_Missing("Layout",51);}
static __attribute__((ms_abi)) int32_t ms_Layout_52(void* self){(void)self;return dw_Missing("Layout",52);}
static __attribute__((ms_abi)) int32_t ms_Layout_53(void* self){(void)self;return dw_Missing("Layout",53);}
static __attribute__((ms_abi)) int32_t ms_Layout_54(void* self){(void)self;return dw_Missing("Layout",54);}
static __attribute__((ms_abi)) int32_t ms_Layout_55(void* self){(void)self;return dw_Missing("Layout",55);}
static __attribute__((ms_abi)) int32_t ms_Layout_56(void* self){(void)self;return dw_Missing("Layout",56);}
static __attribute__((ms_abi)) int32_t ms_Layout_57(void* self){(void)self;return dw_Missing("Layout",57);}
static __attribute__((ms_abi)) int32_t ms_Layout_64(void* self){(void)self;return dw_Missing("Layout",64);}
static __attribute__((ms_abi)) int32_t ms_Layout_65(void* self){(void)self;return dw_Missing("Layout",65);}
static __attribute__((ms_abi)) int32_t ms_Layout_66(void* self){(void)self;return dw_Missing("Layout",66);}
static __attribute__((ms_abi)) int32_t ms_Layout_68(void* self){(void)self;return dw_Missing("Layout",68);}
static __attribute__((ms_abi)) int32_t ms_Layout_70(void* self){(void)self;return dw_Missing("Layout",70);}
const void* const dwLayoutVtable[71]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_SetTextAlign,(const void*)&ms_SetParagraphAlign,(const void*)&ms_SetWrap,(const void*)&ms_SetReading,(const void*)&ms_SetFlow,(const void*)&ms_SetTabStop,(const void*)&ms_SetTrimming,(const void*)&ms_SetLineSpacing,(const void*)&ms_GetTextAlign,(const void*)&ms_GetParagraphAlign,(const void*)&ms_GetWrap,(const void*)&ms_GetReading,(const void*)&ms_GetFlow,(const void*)&ms_GetTabStop,(const void*)&ms_GetTrimming,(const void*)&ms_GetLineSpacing,(const void*)&ms_Layout_19,(const void*)&ms_Layout_20,(const void*)&ms_Layout_21,(const void*)&ms_Layout_22,(const void*)&ms_Layout_23,(const void*)&ms_Layout_24,(const void*)&ms_Layout_25,(const void*)&ms_Layout_26,(const void*)&ms_Layout_27,(const void*)&ms_SetWidth,(const void*)&ms_SetHeight,(const void*)&ms_Layout_30,(const void*)&ms_SetFamily,(const void*)&ms_SetFontWeight,(const void*)&ms_SetFontStyle,(const void*)&ms_Layout_34,(const void*)&ms_SetFontSize,(const void*)&ms_Layout_36,(const void*)&ms_Layout_37,(const void*)&ms_SetEffect,(const void*)&ms_Layout_39,(const void*)&ms_SetTypography,(const void*)&ms_SetLocale,(const void*)&ms_GetWidth,(const void*)&ms_GetHeight,(const void*)&ms_Layout_44,(const void*)&ms_Layout_45,(const void*)&ms_Layout_46,(const void*)&ms_Layout_47,(const void*)&ms_Layout_48,(const void*)&ms_Layout_49,(const void*)&ms_Layout_50,(const void*)&ms_Layout_51,(const void*)&ms_Layout_52,(const void*)&ms_Layout_53,(const void*)&ms_Layout_54,(const void*)&ms_Layout_55,(const void*)&ms_Layout_56,(const void*)&ms_Layout_57,(const void*)&ms_Draw,(const void*)&ms_LineMetrics,(const void*)&ms_TextMetrics,(const void*)&ms_Overhang,(const void*)&ms_ClusterMetrics,(const void*)&ms_MinWidth,(const void*)&ms_Layout_64,(const void*)&ms_Layout_65,(const void*)&ms_Layout_66,(const void*)&ms_PairKerning,(const void*)&ms_Layout_68,(const void*)&ms_Spacing,(const void*)&ms_Layout_70};
static __attribute__((ms_abi)) int32_t ms_Typography_4(void* self){(void)self;return dw_Missing("Typography",4);}
static __attribute__((ms_abi)) int32_t ms_Typography_5(void* self){(void)self;return dw_Missing("Typography",5);}
const void* const dwTypographyVtable[6]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_Feature,(const void*)&ms_Typography_4,(const void*)&ms_Typography_5};
static __attribute__((ms_abi)) int32_t ms_Analysis_5(void* self){(void)self;return dw_Missing("Analysis",5);}
const void* const dwAnalysisVtable[6]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_TextureBounds,(const void*)&ms_Texture,(const void*)&ms_Analysis_5};
const void* const dwStringsVtable[9]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_StringCount,(const void*)&ms_FindLocale,(const void*)&ms_LocaleLength,(const void*)&ms_Locale,(const void*)&ms_StringLength,(const void*)&ms_String};
int32_t dwRenderGlyphRun(void* renderer,void* context,float x,float y,const void* run,const void* description,void* effect){
 typedef int32_t (__attribute__((ms_abi))*Fn)(void*,void*,float,float,uint32_t,const void*,const void*,void*);
 const void* const* table=*(const void* const**)renderer;return ((Fn)table[6])(renderer,context,x,y,0,run,description,effect);
}
// IDWriteInlineObject: Draw, GetMetrics, GetOverhangMetrics, GetBreakConditions.
const void* const dwSignVtable[7]={(const void*)&ms_Query,(const void*)&ms_AddRef,(const void*)&ms_Release,(const void*)&ms_SignDraw,(const void*)&ms_SignMetrics,(const void*)&ms_SignOverhang,(const void*)&ms_SignBreaks};
