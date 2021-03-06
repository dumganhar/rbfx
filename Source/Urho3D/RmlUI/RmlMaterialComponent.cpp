//
// Copyright (c) 2017-2020 the rbfx project.
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
#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Graphics/BillboardSet.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Material.h"
#include "../Graphics/Octree.h"
#include "../Graphics/Renderer.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/Texture2D.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../RmlUI/RmlMaterialComponent.h"
#include "../RmlUI/RmlUI.h"
#include "../Scene/Scene.h"

#include <RmlUi/Core/Context.h>

#include <assert.h>

namespace Urho3D
{

extern const char* RML_UI_CATEGORY;

RmlMaterialComponent::RmlMaterialComponent(Context* context)
    : RmlTextureComponent(context)
{
}

void RmlMaterialComponent::RegisterObject(Context* context)
{
    context->RegisterFactory<RmlMaterialComponent>(RML_UI_CATEGORY);
    URHO3D_COPY_BASE_ATTRIBUTES(BaseClassName);
    URHO3D_ACCESSOR_ATTRIBUTE("Virtual Material Name", GetVirtualMaterialName, SetVirtualMaterialName, ea::string, "", AM_DEFAULT);
    URHO3D_ATTRIBUTE("Remap Mouse Position", bool, remapMousePos_, true, AM_DEFAULT);
}

void RmlMaterialComponent::OnNodeSet(Node* node)
{
    BaseClassName::OnNodeSet(node);
    UpdateVirtualMaterialResource();
}

void RmlMaterialComponent::TranslateMousePos(IntVector2& screenPos)
{
    if (!remapMousePos_ || node_ == nullptr)
        return;

    if (auto* ui = GetSubsystem<RmlUI>())
    {
        Rml::Context* context = ui->GetRmlContext();
        if (!ui->GetBlockEvents() && context->GetHoverElement() != context->GetRootElement())
        {
            // Cursor hovers UI rendered into backbuffer. Do not process any input here.
            screenPos = {-1, -1};
            return;
        }
    }

    Scene* scene = node_->GetScene();
    auto* model = node_->GetComponent<StaticModel>();
    auto* renderer = GetSubsystem<Renderer>();
    auto* octree = scene ? scene->GetComponent<Octree>() : nullptr;
    if (scene == nullptr || model == nullptr || renderer == nullptr || octree == nullptr)
        return;

    Viewport* viewport = nullptr;
    for (int i = 0; i < renderer->GetNumViewports(); i++)
    {
        if (Viewport* vp = renderer->GetViewport(i))
        {
            IntRect rect = vp->GetRect();
            if (vp->GetScene() == scene)
            {
                if (rect == IntRect::ZERO)
                {
                    // Save full-screen viewport only if we do not have a better smaller candidate.
                    if (viewport == nullptr)
                        viewport = vp;
                }
                else if (rect.Contains(screenPos))
                    // Small viewports override full-screen one (picture-in-picture situation).
                    viewport = vp;
                break;
            }
        }
    }

    if (viewport == nullptr)
        return;

    Camera* camera = viewport->GetCamera();
    if (camera == nullptr)
        return;

    IntRect rect = viewport->GetRect();
    if (rect == IntRect::ZERO)
    {
        auto* graphics = GetSubsystem<Graphics>();
        rect.right_ = graphics->GetWidth();
        rect.bottom_ = graphics->GetHeight();
    }

    Ray ray(camera->GetScreenRay((float)screenPos.x_ / rect.Width(), (float)screenPos.y_ / rect.Height()));
    ea::vector<RayQueryResult> queryResultVector;
    RayOctreeQuery query(queryResultVector, ray, RAY_TRIANGLE_UV, M_INFINITY, DRAWABLE_GEOMETRY, DEFAULT_VIEWMASK);
    octree->Raycast(query);
    if (queryResultVector.empty())
        return;

    for (RayQueryResult& queryResult : queryResultVector)
    {
        if (queryResult.drawable_ != model)
        {
            // ignore billboard sets by default
            if (queryResult.drawable_->GetTypeInfo()->IsTypeOf(BillboardSet::GetTypeStatic()))
                continue;
            return;
        }

        Vector2& uv = queryResult.textureUV_;
        IntVector2 uiSize = offScreenUI_->GetRmlContext()->GetDimensions();
        screenPos = IntVector2(static_cast<int>(uv.x_ * uiSize.x_), static_cast<int>(uv.y_ * uiSize.y_));
    }
}

void RmlMaterialComponent::SetVirtualMaterialName(const ea::string& name)
{
    if (material_.NotNull())
        RemoveVirtualResource(material_);
    else
    {
        // Component is being created, material may not exist. Look it up in resource cache first. This solves a problem where removing
        // RmlMaterialComponent in the editor and indoing operation creates a new material while old one is still attached to StaticModel.
        ResourceCache* cache = GetSubsystem<ResourceCache>();
        if (Material* material = cache->GetResource<Material>(name, false))
        {
            material_ = material;
            material_->SetTexture(TU_DIFFUSE, texture_);
        }
        else
            material_ = CreateMaterial();
    }
    material_->SetName(name);
    UpdateVirtualMaterialResource();
}

const ea::string& RmlMaterialComponent::GetVirtualMaterialName() const
{
    assert(material_.NotNull());
    return material_->GetName();
}

void RmlMaterialComponent::UpdateVirtualMaterialResource()
{
    if (material_.Null())
        return;

    if (node_)
        AddVirtualResource(material_);
    else
        RemoveVirtualResource(material_);
}

void RmlMaterialComponent::ApplyAttributes()
{
    if (material_.Null())
        material_ = CreateMaterial();

    BaseClassName::ApplyAttributes();
    UpdateVirtualMaterialResource();
}

Material* RmlMaterialComponent::CreateMaterial() const
{
    Material* material = context_->CreateObject<Material>().Detach();
    material->SetTechnique(0, GetSubsystem<ResourceCache>()->GetResource<Technique>("Techniques/Diff.xml"));
    material->SetTexture(TU_DIFFUSE, texture_);
    return material;
}

void RmlMaterialComponent::OnTextureUpdated()
{
    if (material_.NotNull())
        material_->SetTexture(TU_DIFFUSE, texture_);
}

}
