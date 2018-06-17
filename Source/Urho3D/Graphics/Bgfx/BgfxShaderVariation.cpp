//
// Copyright (c) 2008-2017 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../../Precompiled.h"

#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/ShaderVariation.h"
#include "../../IO/File.h"
#include "../../IO/FileSystem.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include "../../DebugNew.h"

namespace Urho3D
{

static unsigned NumberPostfix(const String& str)
{
	for (unsigned i = 0; i < str.Length(); ++i)
	{
		if (IsDigit(str[i]))
			return ToUInt(str.CString() + i);
	}

	return M_MAX_UNSIGNED;
}

void ShaderVariation::OnDeviceLost()
{
    // No-op on Direct3D9, shaders are preserved through a device loss & reset
}

bool ShaderVariation::Create()
{
    Release();

    if (!graphics_)
        return false;

    if (!owner_)
    {
        compilerOutput_ = "Owner shader has expired";
        return false;
    }

    String shaderPath;
    switch (bgfx::getRendererType())
    {
        case bgfx::RendererType::Noop:
        case bgfx::RendererType::Direct3D9:  shaderPath = "dx9/";   break;
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12: shaderPath = "dx11/";  break;
        case bgfx::RendererType::Gnm:        shaderPath = "pssl/";  break;
        case bgfx::RendererType::Metal:      shaderPath = "metal/"; break;
        case bgfx::RendererType::OpenGL:     shaderPath = "glsl/";  break;
        case bgfx::RendererType::OpenGLES:   shaderPath = "essl/";  break;
        case bgfx::RendererType::Vulkan:     shaderPath = "spirv/"; break;

        case bgfx::RendererType::Count:
            break;
    }

    // Check for up-to-date bytecode on disk
    String path, name, extension;
    SplitPath(owner_->GetName(), path, name, extension);
    extension = type_ == VS ? ".vs" : ".fs";

    String binaryShaderName = graphics_->GetShaderCacheDir() + shaderPath + name + "_" + StringHash(defines_).ToString() + extension;

    if (!LoadByteCode(binaryShaderName))
    {
        // Compile shader if don't have valid bytecode
        if (!Compile())
            return false;
        // Try to load the shader from disk again
        if (!LoadByteCode(binaryShaderName))
            return false;
    }

    // Then create shader from the bytecode
    bgfx::ShaderHandle shdHandle;
    if (byteCode_.Size())
    {
        shdHandle = bgfx::createShader(bgfx::makeRef(&byteCode_[0], byteCode_.Size()));
        object_.idx_ = shdHandle.idx;
		if (!bgfx::isValid(shdHandle))
		{
			if (type_ == VS)
				compilerOutput_ = "Could not create vertex shader";
			else
				compilerOutput_ = "Could not create pixel shader";
		}
        else
        {
            // Now lets get the uniforms
            uint16_t numParameters;
            numParameters = bgfx::getShaderUniforms(shdHandle);
            if (numParameters)
            {
                PODVector<bgfx::UniformHandle> uHandles((unsigned)numParameters);
                bgfx::getShaderUniforms(shdHandle, &uHandles[0], numParameters);
                for (uint16_t i = 0; i < numParameters; ++i)
                {
                    bgfx::UniformInfo info;
                    bgfx::getUniformInfo(uHandles[i], info);

                    // Uniforms have a u_ prefix, so we substring from 2
                    String name = String(info.name).Substring(2);
                    unsigned unit = MAX_TEXTURE_UNITS;
                    // See if it's a sampler
                    if (info.type == bgfx::UniformType::Int1)
                    {
                        unit = graphics_->GetTextureUnit(name);
                        if (unit >= MAX_TEXTURE_UNITS)
                            unit = NumberPostfix(name);

                        if (unit < MAX_TEXTURE_UNITS)
                            useTextureUnits_[unit] = true;
                    }

                    ShaderParameter parameter;
                    parameter.bgfxType_ = (unsigned)info.type;
                    parameter.name_ = name;
                    parameter.type_ = type_;
                    parameter.idx_ = uHandles[i].idx;
                    parameter.texUnit_ = unit;
                    parameters_[StringHash(name)] = parameter;
                }
            }
        }
    }
    else
    {
        if (type_ == VS)
            compilerOutput_ = "Could not create vertex shader, empty bytecode";
        else
            compilerOutput_ = "Could not create pixel shader, empty bytecode";
        shdHandle.idx = bgfx::kInvalidHandle;
        object_.idx_ = shdHandle.idx;
    }

    return bgfx::isValid(shdHandle);
}

void ShaderVariation::Release()
{
    if (object_.idx_ != bgfx::kInvalidHandle)
    {
        if (!graphics_)
            return;

        graphics_->CleanupShaderPrograms(this);

        if (type_ == VS)
        {
            if (graphics_->GetVertexShader() == this)
                graphics_->SetShaders(nullptr, nullptr);
        }
        else
        {
            if (graphics_->GetPixelShader() == this)
                graphics_->SetShaders(nullptr, nullptr);
        }

        // Cleanup uniforms
        for (HashMap<StringHash, ShaderParameter>::ConstIterator i = parameters_.Begin(); i != parameters_.End(); ++i)
        {
            bgfx::UniformHandle uHandle;
            uHandle.idx = i->second_.idx_;
            bgfx::destroy(uHandle);
        }

        bgfx::ShaderHandle handle;
        handle.idx = object_.idx_;
		bgfx::destroy(handle);
        object_.idx_ = bgfx::kInvalidHandle;
    }

    compilerOutput_.Clear();

    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        useTextureUnits_[i] = false;
    parameters_.Clear();
    byteCode_.Clear();
    elementHash_ = 0;
}

void ShaderVariation::SetDefines(const String& defines)
{
    defines_ = defines;
}

bool ShaderVariation::LoadByteCode(const String& binaryShaderName)
{
    ResourceCache* cache = owner_->GetSubsystem<ResourceCache>();
    if (!cache->Exists(binaryShaderName))
        return false;

    FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();
    unsigned sourceTimeStamp = owner_->GetTimeStamp();
    // If source code is loaded from a package, its timestamp will be zero. Else check that binary is not older
    // than source
    if (sourceTimeStamp && fileSystem->GetLastModifiedTime(cache->GetResourceFileName(binaryShaderName)) < sourceTimeStamp)
        return false;

    SharedPtr<File> file = cache->GetFile(binaryShaderName);

    if (file->GetSize())
    {
        byteCode_.Resize(file->GetSize());
        file->Read(&byteCode_[0], byteCode_.Size());

        if (type_ == VS)
            URHO3D_LOGDEBUG("Loaded cached vertex shader " + GetFullName());
        else
            URHO3D_LOGDEBUG("Loaded cached pixel shader " + GetFullName());

        return true;
    }
    else
    {
        URHO3D_LOGERROR(binaryShaderName + " has zero length bytecode");
        return false;
    }
}

bool ShaderVariation::Compile()
{
    String sourceCode = owner_->GetSourceCode(type_);
    Vector<String> defines = defines_.Split(' ');

    String shaderPath;
    switch (bgfx::getRendererType())
    {
    case bgfx::RendererType::Noop:
    case bgfx::RendererType::Direct3D9:  shaderPath = "dx9/";   break;
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12: shaderPath = "dx11/"; defines.Push("D3D11");  break;
    case bgfx::RendererType::Gnm:        shaderPath = "pssl/";  break;
    case bgfx::RendererType::Metal:      shaderPath = "metal/"; break;
    case bgfx::RendererType::OpenGL:     shaderPath = "glsl/";  break;
    case bgfx::RendererType::OpenGLES:   shaderPath = "essl/"; defines.Push("URHO3D_MOBILE");  break;
    case bgfx::RendererType::Vulkan:     shaderPath = "spirv/"; break;

    default:
        break;
    }

    String varying;
    unsigned startPos = sourceCode.Find("#include \"varying_");
    if (startPos != String::NPOS)
    {
        unsigned endPos = sourceCode.Find(".def.sc", startPos);
        if (endPos != String::NPOS)
        {
            varying = sourceCode.Substring(startPos + 10, endPos - startPos - 10);
            sourceCode.Insert(startPos, "//");
            sourceCode.Insert(endPos + 10, "\n");
        }
    }

    if (varying == String::EMPTY)
    {
        return false;
    }

    // Sneaky hack until I work out how to do this right
    Vector<String> varyingTest = varying.Split('_');
    if ((varyingTest[1] == "deferred") && (defines.Contains("DIRLIGHT")))
    {
        varyingTest.Push("dirlight");
        varying.Join(varyingTest, "_");
    }
    else if ((varyingTest[1] == "shadow") && (defines.Contains("VSM_SHADOW")))
    {
        varyingTest.Push("vsm");
        varying.Join(varyingTest, "_");
    }
    else if ((varyingTest[1] == "scenepass") && (defines.Contains("NORMALMAP")))
    {
        varyingTest.Push("normal");
        varying.Join(varyingTest, "_");
    }

    ResourceCache* resourceCache = owner_->GetSubsystem<ResourceCache>();
    String shaderPath2;
    shaderPath2 = resourceCache->GetResourceFileName(owner_->GetName());
    String include, name, extension;
    SplitPath(shaderPath2, include, name, extension);

    String varyingFile = include + varying + ".def.sc";

    // Check for up-to-date bytecode on disk
    String path;
    SplitPath(owner_->GetName(), path, name, extension);
    extension = type_ == VS ? ".vs" : ".fs";

    String intermediateShaderName = graphics_->GetShaderCacheDir() + shaderPath + name + "_" + StringHash(defines_).ToString() + "intermediate" + extension;
    URHO3D_LOGDEBUG("Intermediate shader " + intermediateShaderName);
    String binaryShaderName = graphics_->GetShaderCacheDir() + shaderPath + name + "_" + StringHash(defines_).ToString() + extension;
    URHO3D_LOGDEBUG("Binary shader " + binaryShaderName);

    defines.Push("BGFX_SHADER");

    if (type_ == VS)
        defines.Push("COMPILEVS");
    else
        defines.Push("COMPILEPS");

    //defines.Push("MAXBONES=" + String(Graphics::GetMaxBones()));
    defines.Push("BGFX_CONFIG_MAX_BONES=" + String(Graphics::GetMaxBones()));

    for (unsigned i = 0; i < defines.Size(); ++i)
    {
        // In debug mode, check that all defines are referenced by the shader code
#ifdef _DEBUG
        if (sourceCode.Find(defines[i]) == String::NPOS)
            URHO3D_LOGWARNING("Shader " + GetFullName() + " does not use the define " + defines[i]);
#endif
    }

    File dest(graphics_->GetContext(), intermediateShaderName, FILE_WRITE);
    dest.WriteString(sourceCode);
    dest.Close();

    String shaderc;
    Vector<String> argsArray;
    argsArray.Push("-f");
    argsArray.Push(intermediateShaderName);
    argsArray.Push("-o");
    argsArray.Push(binaryShaderName);
    argsArray.Push("--depends");
    argsArray.Push("-i");
    argsArray.Push(include);
    argsArray.Push("--varyingdef");
    argsArray.Push(varyingFile);
    argsArray.Push("--platform");
	if (bgfx::getRendererType() == bgfx::RendererType::OpenGL)
	{
		argsArray.Push("--profile 120");
		if (!graphics_->GetForceGL2())
		{
			defines.Push("GL3");
		}
	}
#if defined(WIN32)
    argsArray.Push("windows");
    if (bgfx::getRendererType() == bgfx::RendererType::Direct3D11)
    {
		argsArray.Push("--profile");
        argsArray.Push(type_ == VS ? "vs_4_0" : "ps_4_0");
    }
	else if (bgfx::getRendererType() == bgfx::RendererType::Direct3D9)
	{
		argsArray.Push("--profile");
		argsArray.Push(type_ == VS ? "vs_3_0" : "ps_3_0");
	}
	shaderc = "shaderc.exe";
#elif defined(__APPLE__)
	argsArray.Push("osx");
	shaderc = "shaderc";
#else
	argsArray.Push("linux");
	shaderc = "shaderc";
#endif
	argsArray.Push("--type");
	argsArray.Push(type_ == VS ? "vertex" : "fragment");
#if _DEBUG
	argsArray.Push("--debug");
	argsArray.Push("--disasm");
#else
	argsArray.Push("-O");
	argsArray.Push("3");
#endif
	argsArray.Push("--define");
	String defineJoin;
	defineJoin.Join(defines, ";");
	argsArray.Push(defineJoin);

	String args;
	args.Join(argsArray, " ");

	FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();
#ifdef _MSC_VER
#ifdef _DEBUG
	String dirOffset = "../../Debug/";
#else
	String dirOffset = "../../Release/";
#endif
#else
	String dirOffset = "../";
#endif
	String commandLine;
	if(fileSystem->FileExists(fileSystem->GetProgramDir() + shaderc))
		commandLine = fileSystem->GetProgramDir() + shaderc + " " + args;
	else
		commandLine = fileSystem->GetProgramDir() + dirOffset + shaderc + " " + args;
    if(!fileSystem->DirExists(graphics_->GetShaderCacheDir() + shaderPath))
        fileSystem->CreateDir(graphics_->GetShaderCacheDir() + shaderPath);
	URHO3D_LOGDEBUG("Compiling shader command: " + commandLine);

    if (fileSystem->SystemCommand(commandLine, true) == 0)
        return true;
    else
        return false;
}

void ShaderVariation::ParseParameters(unsigned char* bufData, unsigned bufSize)
{

}

void ShaderVariation::SaveByteCode(const String& binaryShaderName)
{

}

}
