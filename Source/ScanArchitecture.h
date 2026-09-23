#pragma once
#include <juce_core/juce_core.h>
#include <windows.h>
namespace lightHostModern::scan
{
inline juce::String nativeLoadFailure(const juce::File& module)
{
    const auto binary=module.isDirectory()?module.getChildFile("Contents/x86_64-win").getChildFile(module.getFileName()):module;
    if(!binary.existsAsFile())return "load_failed:missing_binary";
    const auto library=LoadLibraryW(binary.getFullPathName().toWideCharPointer());
    if(library){FreeLibrary(library);return {};}
    const auto code=GetLastError();
    return "load_failed:"+juce::String((int)code);
}
inline juce::String architectureError(const juce::File& module)
{
    auto files=module.isDirectory()?module.findChildFiles(juce::File::findFiles,true,"*.vst3"):juce::Array<juce::File>{module};
    bool other=false;
    for(const auto& file:files){
        auto in=file.createInputStream();if(!in)continue;
        if(in->readShort()!=IMAGE_DOS_SIGNATURE)continue;
        if(!in->setPosition(0x3c))continue;const auto offset=in->readInt();
        if(offset<0||offset>in->getTotalLength()-6||!in->setPosition(offset)||in->readInt()!=IMAGE_NT_SIGNATURE)continue;
        const auto machine=(unsigned short)in->readShort();
        if(machine==IMAGE_FILE_MACHINE_AMD64)return {};
        other=true;
    }
    return other?"incompatible_architecture":juce::String{};
}
}
