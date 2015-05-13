/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/types/schema.hpp>

#include <pdal/PointLayout.hpp>

#include <entwine/types/simple-point-layout.hpp>

namespace
{
    std::unique_ptr<pdal::PointLayout> makePointLayout(
            std::vector<entwine::DimInfo>& dims)
    {
        std::unique_ptr<pdal::PointLayout> layout(new SimplePointLayout());

        for (auto& dim : dims)
        {
            dim.setId(layout->registerOrAssignDim(dim.name(), dim.type()));
        }

        layout->finalize();

        return layout;
    }
}

namespace entwine
{

Schema::Schema()
    : m_layout(new SimplePointLayout())
    , m_dims()
{ }

Schema::Schema(DimList dims)
    : m_layout(makePointLayout(dims))
    , m_dims(dims)
{ }

Schema::~Schema()
{ }

void Schema::finalize()
{
    m_layout->finalize();

    for (const auto& id : m_layout->dims())
    {
        m_dims.push_back(
                entwine::DimInfo(
                    m_layout->dimName(id),
                    id,
                    m_layout->dimType(id)));
    }
}

std::size_t Schema::pointSize() const
{
    return m_layout->pointSize();
}

const DimList& Schema::dims() const
{
    return m_dims;
}

pdal::PointLayout& Schema::pdalLayout() const
{
    return *m_layout.get();
}

Json::Value Schema::toJson() const
{
    Json::Value json;
    for (const auto& dim : m_dims)
    {
        Json::Value cur;
        cur["name"] = dim.name();
        cur["type"] = dim.typeString();
        cur["size"] = static_cast<Json::UInt64>(dim.size());
        json.append(cur);
    }
    return json;
}

DimList Schema::fromJson(const Json::Value& json)
{
    std::vector<DimInfo> dims;
    for (Json::ArrayIndex i(0); i < json.size(); ++i)
    {
        const Json::Value& jsonDim(json[i]);
        dims.push_back(
                DimInfo(
                    jsonDim["name"].asString(),
                    jsonDim["type"].asString(),
                    jsonDim["size"].asUInt64()));
    }
    return dims;
}

} // namespace entwine

