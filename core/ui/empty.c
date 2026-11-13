/* SPDX-License-Identifier: GPL-3.0-only */

/* A native text view beside the list. Its source is empty while rows exist;
 * its frame follows the list, preserving native horizontal and vertical centering. */

#include "ui/lists.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "ui/templates.h"
#include "osos-2.0.5.h"
#include <stddef.h>

static const void *resource(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(UI_GetResourceBank(), type, id, bytes);
}

static const void *list_frame(const void *instances)
{
    struct core_block instance, view;
    for (uint32_t i = 0; core_block_at(instances, i, &instance); i++) {
        if (core_word_at(instance.data, SsinInstance_template) != RES_SettingsList_Template)
            continue;
        uint32_t bytes;
        const void *layout = resource(Resource_VLyt,
            core_word_at(instance.data, SsinInstance_layout_events_id), &bytes);
        for (uint32_t v = 0; layout && core_block_at(layout, v, &view); v++)
            if (view.kind == RES_SettingsList_View)
                return view.data;
    }
    return NULL;
}

int core_list_empty(struct core_screen *s, uint32_t layout, uint32_t *next,
                    uint8_t *scratch, uint32_t bytes)
{
    uint32_t size, template = core_template(CORE_TEMPLATE_EMPTY);
    struct core_block list, prototype;
    const void *layouts = resource(Resource_SLyt, layout, &size);
    const void *original = resource(Resource_SSin, RES_Calendar_Empty_Instances, &size);
    if (!template || !layouts || !original ||
        !core_block_at(layouts, 0, &list) || !core_block_at(original, 1, &prototype))
        return 0;
    uint32_t instances_id = core_word_at(list.data, 0);
    const void *instances = resource(Resource_SSin, instances_id, &size);
    const void *frame_options = instances ? list_frame(instances) : NULL;
    if (!frame_options || size + prototype.bytes + 8 > bytes)
        return 0;
    uint32_t edges = 0;
    struct core_block edge;
    for (uint32_t i = 0; core_block_at(frame_options, i, &edge); i++)
        if (edge.kind >= ViewOption_TopEdge && edge.kind <= ViewOption_RightEdge)
            edges++;
    if (edges != 4)
        return 0;
    uint32_t binding = (*next)++;
    core_set_source(s->id + 3, s->id + 3, SourceDestination_Title);
    struct core_writer w = {scratch, 0};
    core_write_word(&w, 1);
    uint32_t view = core_write_block_begin(&w, template + 1);
    core_write_word(&w, 1 + edges);
    uint32_t option = core_write_block_begin(&w, Resource_SORC);
    core_write_word(&w, s->id + 3);
    core_write_block_end(&w, option);
    for (uint32_t i = 0; core_block_at(frame_options, i, &edge); i++) {
        if (edge.kind < ViewOption_TopEdge || edge.kind > ViewOption_RightEdge)
            continue;
        option = core_write_block_begin(&w, edge.kind);
        core_write_bytes(&w, edge.data, edge.bytes);
        core_write_block_end(&w, option);
    }
    core_write_block_end(&w, view);
    core_resource_set(Resource_VCvs, binding, scratch, w.at);
    const void *frame = resource(Resource_VCrv,
        core_word_at(prototype.data, SsinInstance_view_bindings), &size);
    if (frame)
        core_resource_set(Resource_VCrv, binding, frame, size);
    w.at = 0;
    uint32_t count = core_blocks_count(instances);
    core_write_word(&w, count + 1);
    for (uint32_t i = 0; i < count; i++) {
        struct core_block instance;
        core_block_at(instances, i, &instance);
        uint32_t begin = core_write_block_begin(&w, instance.kind);
        core_write_bytes(&w, instance.data, instance.bytes);
        core_write_block_end(&w, begin);
    }
    uint32_t words[] = {SsinInstance_view_bindings, binding,
                        SsinInstance_template, template};
    uint32_t begin = core_write_block_begin(&w, prototype.kind);
    core_write_copy(&w, prototype.data, prototype.bytes, words, 2);
    core_write_block_end(&w, begin);
    core_resource_set(Resource_SSin, instances_id, scratch, w.at);
    return 1;
}
