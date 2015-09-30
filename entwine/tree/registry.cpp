/******************************************************************************
* Copyright (c) 2016, Connor Manning (connor@hobu.co)
*
* Entwine -- Point cloud indexing
*
* Entwine is available under the terms of the LGPL2 license. See COPYING
* for specific license text and more information.
*
******************************************************************************/

#include <entwine/tree/registry.hpp>

#include <algorithm>

#include <pdal/PointView.hpp>

#include <entwine/third/json/json.hpp>
#include <entwine/third/arbiter/arbiter.hpp>
#include <entwine/tree/chunk.hpp>
#include <entwine/tree/climber.hpp>
#include <entwine/tree/clipper.hpp>
#include <entwine/tree/cold.hpp>
#include <entwine/tree/point-info.hpp>
#include <entwine/types/bbox.hpp>
#include <entwine/types/schema.hpp>
#include <entwine/types/simple-point-table.hpp>
#include <entwine/util/pool.hpp>

namespace entwine
{

namespace
{
    bool better(
            const Point& candidate,
            const Point& current,
            const Point& goal,
            const bool is3d)
    {
        if (is3d)
        {
            return candidate.sqDist3d(goal) < current.sqDist3d(goal);
        }
        else
        {
            return candidate.sqDist2d(goal) < current.sqDist2d(goal);
        }
    }

    const std::size_t clipPoolSize(1);
    const std::size_t clipQueueSize(1);
}

Registry::Registry(
        arbiter::Endpoint& endpoint,
        const Schema& schema,
        const BBox& bbox,
        const Structure& structure)
    : m_endpoint(endpoint)
    , m_schema(schema)
    , m_bbox(bbox)
    , m_structure(structure)
    , m_is3d(structure.is3d())
    , m_base()
    , m_cold()
    , m_pool(new Pool(clipPoolSize, clipQueueSize))
{
    if (m_structure.baseIndexSpan())
    {
        m_base.reset(
                static_cast<ContiguousChunk*>(
                    Chunk::create(
                        m_schema,
                        m_bbox,
                        m_structure,
                        0,
                        m_structure.baseIndexBegin(),
                        m_structure.baseIndexSpan(),
                        true).release()));
    }

    if (m_structure.hasCold())
    {
        m_cold.reset(new Cold(endpoint, schema, m_bbox, m_structure));
    }
}

Registry::Registry(
        arbiter::Endpoint& endpoint,
        const Schema& schema,
        const BBox& bbox,
        const Structure& structure,
        const Json::Value& meta)
    : m_endpoint(endpoint)
    , m_schema(schema)
    , m_bbox(bbox)
    , m_structure(structure)
    , m_is3d(structure.is3d())
    , m_base()
    , m_cold()
    , m_pool(new Pool(clipPoolSize, clipQueueSize))
{
    if (m_structure.baseIndexSpan())
    {
        const std::string basePath(
                m_structure.baseIndexBegin().str() +
                m_structure.subsetPostfix());

        std::vector<char> data(m_endpoint.getSubpathBinary(basePath));

        m_base.reset(
                static_cast<ContiguousChunk*>(
                    Chunk::create(
                        m_schema,
                        m_bbox,
                        m_structure,
                        0,
                        m_structure.baseIndexBegin(),
                        m_structure.baseIndexSpan(),
                        data).release()));
    }

    if (m_structure.hasCold())
    {
        m_cold.reset(new Cold(endpoint, schema, m_bbox, m_structure, meta));
    }
}

Registry::~Registry()
{ }

bool Registry::addPoint(
        std::unique_ptr<PointInfo> toAdd,
        Climber& climber,
        Clipper* clipper)
{
    bool done(false);

    if (Cell* cell = getCell(climber, clipper))
    {
        bool redo(false);
        PointInfo* toAddSaved(toAdd.get());

        do
        {
            const PointInfoAtom& atom(cell->atom());
            if (PointInfo* current = atom.load())
            {
                const Point& mid(climber.bbox().mid());

                // TODO
                if (better(toAdd->point(), current->point(), mid, true/*m_is3d*/))
                {
                    done = false;
                    redo = !cell->swap(std::move(toAdd), atom);
                    if (!redo) toAdd.reset(current);
                }
            }
            else
            {
                done = cell->swap(std::move(toAdd));
                redo = !done;
            }

            if (redo) toAdd.reset(toAddSaved);
        }
        while (redo);
    }

    if (done)
    {
        return true;
    }
    else
    {
        climber.magnify(toAdd->point());
        if (!m_structure.inRange(climber.index())) return false;
        else return addPoint(std::move(toAdd), climber, clipper);
    }
}

Cell* Registry::getCell(const Climber& climber, Clipper* clipper)
{
    Cell* cell(0);

    const Id& index(climber.index());

    if (m_structure.isWithinBase(index))
    {
        cell = &m_base->getCell(climber);
    }
    else if (m_structure.isWithinCold(index))
    {
        cell = &m_cold->getCell(climber, clipper);
    }

    return cell;
}

void Registry::clip(
        const Id& index,
        const std::size_t chunkNum,
        Clipper* clipper)
{
    m_cold->clip(index, chunkNum, clipper, *m_pool);
}

void Registry::save(Json::Value& meta)
{
    m_base->save(m_endpoint, m_structure.subsetPostfix());
    m_base.reset();

    if (m_cold) meta["ids"] = m_cold->toJson();
}

} // namespace entwine

