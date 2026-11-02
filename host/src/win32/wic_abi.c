// The vtables of the host's WIC objects (Wic.cpp) as Windows code sees them:
// one Windows x64 function per method, forwarding to the host's System V
// implementation. Compiled for x86_64-pc-freebsd like guest_abi.c. Slots are
// wincodec.h's order; a method the host does not implement returns E_NOTIMPL
// and names itself in the log.
#include <stdint.h>
#include "WicAbi.h"

void hostGateNamed(const char* name);

#define METHOD(name,ret,params,args) \
    ret wic_##name params; \
    static __attribute__((ms_abi)) ret ms_##name params {hostGateNamed(#name);return wic_##name args;}
#define MISSING(object,slot) \
    static __attribute__((ms_abi)) int32_t ms_##object##_##slot(void){hostGateNamed(#object);return wicMissing(#object,slot);}
#define UNKNOWN (const void*)&ms_QueryInterface,(const void*)&ms_AddRef,(const void*)&ms_Release

// IUnknown, shared by every object.
METHOD(QueryInterface,int32_t,(void* self,const void* id,void** out),(self,id,out))
METHOD(AddRef,uint32_t,(void* self),(self))
METHOD(Release,uint32_t,(void* self),(self))

// IWICImagingFactory
METHOD(CreateDecoderFromFilename,int32_t,(void* self,const uint16_t* name,const void* vendor,uint32_t access,uint32_t options,void** decoder),(self,name,vendor,access,options,decoder))
METHOD(CreateDecoderFromStream,int32_t,(void* self,void* stream,const void* vendor,uint32_t options,void** decoder),(self,stream,vendor,options,decoder))
MISSING(Factory,5)    // CreateDecoderFromFileHandle
MISSING(Factory,6)    // CreateComponentInfo
MISSING(Factory,7)    // CreateDecoder
MISSING(Factory,8)    // CreateEncoder
MISSING(Factory,9)    // CreatePalette
METHOD(CreateFormatConverter,int32_t,(void* self,void** converter),(self,converter))
MISSING(Factory,11)   // CreateBitmapScaler
MISSING(Factory,12)   // CreateBitmapClipper
MISSING(Factory,13)   // CreateBitmapFlipRotator
METHOD(CreateStream,int32_t,(void* self,void** stream),(self,stream))
MISSING(Factory,15)   // CreateColorContext
MISSING(Factory,16)   // CreateColorTransformer
MISSING(Factory,17)   // CreateBitmap
MISSING(Factory,18)   // CreateBitmapFromSource
MISSING(Factory,19)   // CreateBitmapFromSourceRect
MISSING(Factory,20)   // CreateBitmapFromMemory
MISSING(Factory,21)   // CreateBitmapFromHBITMAP
MISSING(Factory,22)   // CreateBitmapFromHICON
MISSING(Factory,23)   // CreateComponentEnumerator
MISSING(Factory,24)   // CreateFastMetadataEncoderFromDecoder
MISSING(Factory,25)   // CreateFastMetadataEncoderFromFrameDecode
MISSING(Factory,26)   // CreateQueryWriter
MISSING(Factory,27)   // CreateQueryWriterFromReader
const void* const guestWicFactoryVtable[WicFactorySlots]={
    UNKNOWN,(const void*)&ms_CreateDecoderFromFilename,(const void*)&ms_CreateDecoderFromStream,(const void*)&ms_Factory_5,(const void*)&ms_Factory_6,
    (const void*)&ms_Factory_7,(const void*)&ms_Factory_8,(const void*)&ms_Factory_9,(const void*)&ms_CreateFormatConverter,
    (const void*)&ms_Factory_11,(const void*)&ms_Factory_12,(const void*)&ms_Factory_13,(const void*)&ms_CreateStream,
    (const void*)&ms_Factory_15,(const void*)&ms_Factory_16,(const void*)&ms_Factory_17,(const void*)&ms_Factory_18,
    (const void*)&ms_Factory_19,(const void*)&ms_Factory_20,(const void*)&ms_Factory_21,(const void*)&ms_Factory_22,
    (const void*)&ms_Factory_23,(const void*)&ms_Factory_24,(const void*)&ms_Factory_25,(const void*)&ms_Factory_26,
    (const void*)&ms_Factory_27,
};

// IWICStream (ISequentialStream, IStream, IWICStream) over memory.
METHOD(StreamRead,int32_t,(void* self,void* data,uint32_t size,uint32_t* done),(self,data,size,done))
MISSING(Stream,4)     // Write
METHOD(StreamSeek,int32_t,(void* self,int64_t move,uint32_t origin,uint64_t* position),(self,move,origin,position))
MISSING(Stream,6)     // SetSize
MISSING(Stream,7)     // CopyTo
MISSING(Stream,8)     // Commit
MISSING(Stream,9)     // Revert
MISSING(Stream,10)    // LockRegion
MISSING(Stream,11)    // UnlockRegion
METHOD(StreamStat,int32_t,(void* self,void* stat,uint32_t flags),(self,stat,flags))
MISSING(Stream,13)    // Clone
MISSING(Stream,14)    // InitializeFromIStream
MISSING(Stream,15)    // InitializeFromFilename
METHOD(InitializeFromMemory,int32_t,(void* self,uint8_t* data,uint32_t size),(self,data,size))
MISSING(Stream,17)    // InitializeFromIStreamRegion
const void* const guestWicStreamVtable[WicStreamSlots]={
    UNKNOWN,(const void*)&ms_StreamRead,(const void*)&ms_Stream_4,(const void*)&ms_StreamSeek,(const void*)&ms_Stream_6,
    (const void*)&ms_Stream_7,(const void*)&ms_Stream_8,(const void*)&ms_Stream_9,(const void*)&ms_Stream_10,(const void*)&ms_Stream_11,
    (const void*)&ms_StreamStat,(const void*)&ms_Stream_13,(const void*)&ms_Stream_14,(const void*)&ms_Stream_15,
    (const void*)&ms_InitializeFromMemory,(const void*)&ms_Stream_17,
};

// IWICBitmapDecoder
METHOD(QueryCapability,int32_t,(void* self,void* stream,uint32_t* capability),(self,stream,capability))
METHOD(DecoderInitialize,int32_t,(void* self,void* stream,uint32_t options),(self,stream,options))
METHOD(GetContainerFormat,int32_t,(void* self,void* format),(self,format))
MISSING(Decoder,6)    // GetDecoderInfo
MISSING(Decoder,7)    // CopyPalette
MISSING(Decoder,8)    // GetMetadataQueryReader
MISSING(Decoder,9)    // GetPreview
METHOD(DecoderGetColorContexts,int32_t,(void* self,uint32_t count,void** contexts,uint32_t* actual),(self,count,contexts,actual))
MISSING(Decoder,11)   // GetThumbnail
METHOD(GetFrameCount,int32_t,(void* self,uint32_t* count),(self,count))
METHOD(GetFrame,int32_t,(void* self,uint32_t index,void** frame),(self,index,frame))
const void* const guestWicDecoderVtable[WicDecoderSlots]={
    UNKNOWN,(const void*)&ms_QueryCapability,(const void*)&ms_DecoderInitialize,(const void*)&ms_GetContainerFormat,
    (const void*)&ms_Decoder_6,(const void*)&ms_Decoder_7,(const void*)&ms_Decoder_8,(const void*)&ms_Decoder_9,
    (const void*)&ms_DecoderGetColorContexts,(const void*)&ms_Decoder_11,(const void*)&ms_GetFrameCount,(const void*)&ms_GetFrame,
};

// IWICBitmapSource, shared by the frame and the converter.
METHOD(GetSize,int32_t,(void* self,uint32_t* width,uint32_t* height),(self,width,height))
METHOD(GetPixelFormat,int32_t,(void* self,void* format),(self,format))
METHOD(GetResolution,int32_t,(void* self,double* x,double* y),(self,x,y))
MISSING(Source,6)     // CopyPalette
METHOD(CopyPixels,int32_t,(void* self,const void* rect,uint32_t stride,uint32_t size,uint8_t* buffer),(self,rect,stride,size,buffer))
#define SOURCE (const void*)&ms_GetSize,(const void*)&ms_GetPixelFormat,(const void*)&ms_GetResolution,(const void*)&ms_Source_6,(const void*)&ms_CopyPixels

// IWICBitmapFrameDecode
MISSING(Frame,8)      // GetMetadataQueryReader
METHOD(FrameGetColorContexts,int32_t,(void* self,uint32_t count,void** contexts,uint32_t* actual),(self,count,contexts,actual))
MISSING(Frame,10)     // GetThumbnail
const void* const guestWicFrameVtable[WicFrameSlots]={
    UNKNOWN,SOURCE,(const void*)&ms_Frame_8,(const void*)&ms_FrameGetColorContexts,(const void*)&ms_Frame_10,
};

// IWICFormatConverter
METHOD(ConverterInitialize,int32_t,(void* self,void* source,const void* format,uint32_t dither,void* palette,double alpha,uint32_t paletteType),
       (self,source,format,dither,palette,alpha,paletteType))
METHOD(CanConvert,int32_t,(void* self,const void* from,const void* to,int32_t* can),(self,from,to,can))
const void* const guestWicConverterVtable[WicConverterSlots]={
    UNKNOWN,SOURCE,(const void*)&ms_ConverterInitialize,(const void*)&ms_CanConvert,
};
