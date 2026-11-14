/* SPDX-License-Identifier: GPL-3.0-only */

/* The preview panes of the menus with a sidebar. Every stock row of Main,
 * Music, Videos, Extras and Settings has a layout of its own whose pane shows
 * that row's picture and caption, and the row's delayedselected binding
 * switches to it. Rows extensions add share layouts instead, one per menu and
 * pane kind: their binding names the row's pane handler and switches to the
 * menu's pane name, which the handler points at the layout of the kind the
 * row's picture asks for, having made the row its current one; the layout's
 * pane views are bound to properties answered from the row (label, caption,
 * bitmap). The kind follows the picture's size, as Apple's panes do (Games on
 * Main shows the Extras pane): a 78 x 121 mask is a Settings icon, the other
 * sizes are the pictures of the kinds below. A menu's layout of a kind is
 * built on the first highlight of a row whose picture asks for it and added to
 * the menu's running controller, so a menu nobody highlights an extension row
 * on costs nothing. A music or video item without pane fields binds the stock
 * highlight handler instead (core_panes_stock_handler): Apple's handler emits
 * the controller event that switches to the stock count, artwork or empty
 * pane, and refreshes it when the artwork changes. */

#include "ui/panes.h"
#include "osos-2.0.5.h"
#include "ui/bitmaps.h"
#include "ui/clones.h"
#include "ui/indexed.h"
#include "ui/lists.h"
#include "ui/menus.h"
#include "ui/resources.h"
#include "ui/screens.h"
#include "ui/templates.h"
#include <stddef.h>

/* The pane kinds, each with the picture its pane shows, the stock template
 * that shows it and what each of the template's views shows, in the View
 * resource's order (the icon pane's in the order core_panes_write binds its
 * VLyt). A kind every row of a menu uses (icon, square) binds the stock
 * template itself through the row layout's VLyt or VCvs; a kind whose stock
 * rows each have a template of their own gets a copy of one of them (with a
 * second template's views after it when the stock pane is two instances), so
 * the stock views stay untouched. */
enum {
    PANE_ICON,       /* a 78 x 121 mask on the Settings box, the label above */
    PANE_SQUARE,     /* 119 x 119, the caption under it */
    PANE_WIDE,       /* 142 x 87, the caption under it */
    PANE_LINES_OVER, /* 130 x 128, the label above, two lines over it, the caption under */
    PANE_BADGED,     /* 146 x 110, a count line with a badge before it under */
    PANE_CLOCK_BOX,  /* 132 x 145 in the World Clock pane's clock box, two lines above */
    PANE_KINDS
};
/* What a view shows: CAPTION falls back to the label, CAPTION_ONLY stays empty
 * without one, for the kinds whose title line shows the label. */
enum { PICTURE, CAPTION, CAPTION_ONLY, LABEL, LINE, LINE2, BADGE, BOX };
#define MAX_VIEWS 5

static const struct kind {
    uint8_t width, height;
    uint8_t views;
    uint8_t roles[MAX_VIEWS];
    uint32_t template;
    uint32_t second;   /* template whose views the copy gets after `template`'s */
    uint32_t bindings; /* the shared template's VLyt; 0 for a kind that copies its template */
    uint32_t layout;   /* stock row layout cloned in place of the menu's, for its other instances */
    int8_t replaced;   /* view of the copy that becomes the picture view, 0 for none */
} kinds[PANE_KINDS] = {
    [PANE_ICON] = {78, 121, 4, {PICTURE, CAPTION_ONLY, LABEL, BOX},
                   .template = RES_SettingsPreview_Template},
    [PANE_SQUARE] = {119, 119, 2, {PICTURE, CAPTION}, .template = RES_MediaMenuPreview_Template,
                     .bindings = RES_MediaMenuPreview_Bindings},
    [PANE_WIDE] = {142, 87, 2, {PICTURE, CAPTION}, .template = RES_ExtrasPreview_Games_Template},
    [PANE_LINES_OVER] = {130, 128, 5, {PICTURE, LINE, LINE2, LABEL, CAPTION_ONLY},
                         .template = RES_ExtrasPreview_Calendars_Template,
                         .second = RES_ExtrasPreview_Calendars_Lines_Template},
    [PANE_BADGED] = {146, 110, 3, {PICTURE, CAPTION, BADGE},
                     .template = RES_MainMenuPreview_Radio_Template},
    /* City, region and date lines, then the clock view, which the copy makes
     * an image view in the clock's box. */
    [PANE_CLOCK_BOX] = {132, 145, 4, {LABEL, LINE, CAPTION_ONLY, PICTURE},
                        .template = RES_ExtrasPreview_Clocks_Template,
                        .layout = RES_ExtrasMenu_Clocks_Layout, .replaced = 3},
};
static uint32_t copies[PANE_KINDS]; /* the template copies made so far */

/* A menu with a pane: the stock row layout its shared layouts clone, the screen
 * whose SLst lists it, the pane instance's template in that layout, the kind
 * a row without a picture gets, the layout name the rows' bindings switch to
 * and the offset at which the menu's controller class embeds its screen
 * definition. The icon
 * pane is rebound through a VLyt of its four views, the others through the
 * pane instance's VCvs, binding the template's views from its first image
 * view on. */
struct menu {
    uint8_t root;
    uint8_t kind;
    uint16_t definition_offset;
    uint32_t stock;
    uint32_t screen;
    uint32_t template;
    const char *name;
};

static const struct menu menus[] = {
    {RV_MENU_SETTINGS, PANE_ICON, SettingsMenuListController_screen_definition,
     RES_SettingsMenus_Legal_Layout, RES_SettingsMenu_Main_Screen, RES_SettingsPreview_Template,
     "RvSettingsPane"},
    {RV_MENU_MAIN, PANE_SQUARE, MainMediaMenuController_screen_definition,
     RES_MainMenus_Main_Radio_Layout, RES_MainMenus_Main_Screen, RES_MainMenuPreview_Radio_Template,
     "RvMainPane"},
    {RV_MENU_MUSIC, PANE_SQUARE, MainMediaMenuController_screen_definition, RES_MusicMenu_Songs_Layout,
     RES_MainMenus_Music_Screen, RES_MediaMenuPreview_Template, "RvMusicPane"},
    {RV_MENU_VIDEOS, PANE_SQUARE, MainMediaMenuController_screen_definition,
     RES_VideosMenu_Movies_Layout, RES_VideosMenu_Screen, RES_MediaMenuPreview_Template,
     "RvVideosPane"},
    {RV_MENU_EXTRAS, PANE_WIDE, ExtrasMenuController_screen_definition, RES_ExtrasMenu_Games_Layout,
     RES_ExtrasMenu_Screen, RES_ExtrasPreview_Games_Template, "RvExtrasPane"},
};
#define MENUS (sizeof menus / sizeof menus[0])

/* A shared layout built so far, one per menu and kind. */
struct pane {
    struct pane *next;
    uint8_t menu, kind;
    uint32_t layout;  /* the layout's ids follow it */
    uint32_t current; /* item id of the row the pane shows */
    /* The stock row layout's SLst entry with the clone's id: every Layout
     * built for the clone keeps a pointer to it. */
    uint32_t record[ScreenLayoutEntry_bytes / 4];
};
static struct pane *panes;
static uint32_t pointed[MENUS]; /* the layout each menu's pane name points at */

/* Ids after a layout: three for its SSin, its pane VLyt or VCvs and a status
 * bar VCvs, then a source per pane view, in the kind's view order. */
#define SOURCES(p) ((p)->layout + 4)
#define IDS (4 + MAX_VIEWS)

static void *bank;
static uint32_t list_pane; /* caption id of the last list pane shown, 0 before one */

static const void *stock(uint32_t type, uint32_t id, uint32_t *bytes)
{
    return ResourceBank_GetResource(bank, type, id, bytes);
}

static const struct menu *menu_of(uint8_t root)
{
    for (uint32_t i = 0; i < MENUS; i++)
        if (menus[i].root == root)
            return &menus[i];
    return NULL;
}

/* The menus the MainMediaMenu controller runs, the ones with media items. */
static int media_menu(uint8_t root)
{
    return root == RV_MENU_MAIN || root == RV_MENU_MUSIC || root == RV_MENU_VIDEOS;
}

const char *core_panes_stock_handler(uint8_t root, const struct rv_menu_entry *e)
{
    if (!e || !media_menu(root) || e->caption || e->caption_text || e->bitmap || e->bitmap_for ||
        e->lines[0] || e->lines[1] || e->line_text || e->badge || e->badge_for)
        return NULL;
    uint32_t preview = root == RV_MENU_MUSIC    ? RV_PREVIEW_MUSIC
                       : root == RV_MENU_VIDEOS ? RV_PREVIEW_VIDEOS
                                                : e->preview;
    /* The handlers of the stock Music and Videos rows (Cover Flow, Search, the
     * Music and Videos rows on Main), from the menu screens' event tables. */
    return preview == RV_PREVIEW_MUSIC    ? "HandleMusicHilited"
           : preview == RV_PREVIEW_VIDEOS ? "HandleVideosHilited"
                                          : NULL;
}

static struct pane *pane_of(const struct menu *m, uint32_t kind)
{
    for (struct pane *p = panes; p; p = p->next)
        if (&menus[p->menu] == m && p->kind == kind)
            return p;
    return NULL;
}

/* Word `field` of a stock layout's instance of `template`, 0 for none. */
static uint32_t instance_word(uint32_t layout_id, uint32_t template, uint32_t field)
{
    uint32_t bytes;
    const void *slyt = stock(Resource_SLyt, layout_id, &bytes);
    for (uint32_t l = 0; slyt && l < core_blocks_count(slyt); l++) {
        struct core_block list, instance;
        core_block_at(slyt, l, &list);
        const void *ssin = stock(Resource_SSin, core_word_at(list.data, 0), &bytes);
        for (uint32_t i = 0; ssin && core_block_at(ssin, i, &instance); i++)
            if (core_word_at(instance.data, SsinInstance_template) == template)
                return core_word_at(instance.data, field);
    }
    return 0;
}

/* The VLyt a stock layout's instance of `template` names, 0 for none. */
static uint32_t pane_layout(uint32_t layout_id, uint32_t template)
{
    return instance_word(layout_id, template, SsinInstance_layout_events_id);
}

/* The title source a stock layout's status bar text binds, 0 for none. */
static uint32_t title_source(uint32_t layout_id)
{
    uint32_t bytes;
    uint32_t vcvs_id = instance_word(layout_id, RES_StatusBarWhite_Template, SsinInstance_view_bindings);
    const void *vcvs = vcvs_id ? stock(Resource_VCvs, vcvs_id, &bytes) : NULL;
    struct core_block view, option;
    if (!vcvs || !core_block_at(vcvs, 0, &view) || view.kind != RES_StatusBarWhite_Text)
        return 0;
    for (uint32_t o = 0; core_block_at(view.data, o, &option); o++)
        if (option.kind == Resource_SORC)
            return core_word_at(option.data, 0);
    return 0;
}

/* The first image view of a stock template: its id, and in *index its position. */
static uint32_t image_view(uint32_t template, uint32_t *index)
{
    uint32_t bytes;
    struct core_block view;
    const void *views = stock(Resource_View, template, &bytes);
    for (*index = 0; views && core_block_at(views, *index, &view); (*index)++)
        if (core_word_at(view.data, ViewDescriptor_type) == ViewType_Image)
            return core_word_at(view.data, ViewDescriptor_id);
    return 0;
}

uint32_t core_panes_write(struct core_writer *w, uint32_t title, uint32_t line, uint32_t icon,
                          uint32_t box)
{
    uint32_t bytes,
        stock_pane = pane_layout(RES_Settings_MainMenu_Layout, RES_SettingsPreview_Template);
    const void *vlyt = stock_pane ? stock(Resource_VLyt, stock_pane, &bytes) : NULL;
    if (!vlyt)
        return 0;
    uint32_t views = core_blocks_count(vlyt);
    core_write_word(w, views);
    for (uint32_t v = 0; v < views; v++) {
        struct core_block view, option;
        core_block_at(vlyt, v, &view);
        uint32_t source = view.kind == RES_SettingsPreview_Caption_Text      ? title
                          : view.kind == RES_Settings_InfoDetail_View        ? line
                          : view.kind == RES_Settings_Icon_View              ? icon
                          : view.kind == RES_SettingsPreview_Background_View ? box
                                                                             : 0;
        /* The box's bitmap comes from the template; the stock layouts only show
         * or hide it, so a source for it is an option of its own. */
        int add_source = view.kind == RES_SettingsPreview_Background_View && box;
        uint32_t begin = core_write_block_begin(w, view.kind);
        core_write_word(w, core_blocks_count(view.data) + (uint32_t)add_source);
        for (uint32_t o = 0; core_block_at(view.data, o, &option); o++) {
            uint32_t b = core_write_block_begin(w, option.kind);
            if (option.kind == Resource_SORC && source)
                core_write_word(w, source);
            else if (option.kind == ViewOption_Visible)
                core_write_bytes(w, source ? "\1" : "", 1);
            else
                core_write_bytes(w, option.data, option.bytes);
            core_write_block_end(w, b);
        }
        if (add_source) {
            uint32_t b = core_write_block_begin(w, Resource_SORC);
            core_write_word(w, box);
            core_write_block_end(w, b);
        }
        core_write_block_end(w, begin);
    }
    return stock_pane;
}

void core_panes_store(uint32_t id, const void *data, uint32_t bytes, uint32_t stock_pane)
{
    uint32_t events_bytes;
    const void *events = stock(Resource_TEVT, stock_pane, &events_bytes);
    core_resource_set(Resource_VLyt, id, data, bytes);
    if (events) /* the layout's event tables are loaded by this id too */
        core_resource_set(Resource_TEVT, id, events, events_bytes);
}

/* The icon pane of a clone: a VLyt binding the four views, whose sources follow
 * the kind's role order: the icon view, the line under it, the title line above
 * it and the box behind the icon. */
static int icon_pane(const struct menu *m, uint32_t id, struct layout_clone *c,
                         struct core_writer *w)
{
    uint32_t sources = id + 4;
    uint32_t stock_pane = core_panes_write(w, sources + 2, sources + 1, sources, sources + 3);
    c->from = pane_layout(m->stock, m->template);
    c->to = id + 2;
    if (!stock_pane || !c->from)
        return 0;
    core_panes_store(c->to, w->out, w->at, c->from);
    for (uint32_t i = 0; i < 4; i++)
        core_set_source(sources + i, sources + i, SourceDestination_Title);
    return 1;
}

/* A picture pane of a clone: the kind's template in the pane instance's place,
 * its picture and caption views bound through the instance's VCvs. */
static int picture_pane(uint32_t kind, uint32_t id, struct layout_clone *c,
                        struct view_sources *views)
{
    const struct kind *k = &kinds[kind];
    uint32_t sources = id + 4, index;
    if (!k->template)
        return 0;
    if (k->bindings) {
        c->to = k->template;
        c->bindings = k->bindings;
        views->view = image_view(k->template, &index);
    } else {
        if (!copies[kind])
            copies[kind] = core_templates_copy(k->template, k->second, k->replaced ? k->replaced : -1);
        if (!copies[kind])
            return 0;
        c->to = copies[kind];
        /* The instance needs a layout events id with a TEVT, or loading the
         * layout's events aborts; Games' has one and no VLyt that would rebind
         * the copy's views, so every copy takes it. */
        c->bindings = pane_layout(RES_ExtrasMenu_Games_Layout, RES_ExtrasPreview_Games_Template);
        views->view = copies[kind] + 1;
    }
    views->source = sources;
    views->count = k->views;
    c->extra = views;
    for (uint32_t i = 0; i < k->views; i++)
        core_set_source(sources + i, sources + i, SourceDestination_Title);
    return views->view != 0;
}

/* The screen definition of the menu's controller on screen, NULL when
 * `controller` runs another screen. */
static uint8_t *definition_of(const struct menu *m, void *controller)
{
    uint8_t *definition = (uint8_t *)controller + m->definition_offset;
    return core_word_at(definition, ScreenDefinition_screen) == m->screen ? definition : NULL;
}

/* A shared layout: the menu's stock row layout with its pane rebound for the
 * kind, listed in the menu screen's SLst. Apple reads the SLst on every push
 * and, after every layout change, builds the screen's definition from it
 * again to look the shown layout up, so a layout only a controller knows is
 * not enough. Its events are the stock layout's, read when Apple loads them,
 * so it stores none of its own. NULL when it cannot be built. */
static struct pane *build(const struct menu *m, uint32_t kind, void *controller)
{
    uint32_t slst_bytes, entry_bytes, none = 0;
    /* A kind on another menu's row layout carries this menu's title in its
     * status bar; a menu whose status bar text binds no source (Main) cannot
     * have it. */
    uint32_t title = kinds[kind].layout ? title_source(m->stock) : 0;
    const void *slst = stock(Resource_SLst, m->screen, &slst_bytes);
    const uint8_t *entry = slst ? core_block_with_word(slst, 0, m->stock, &entry_bytes) : NULL;
    if (!entry || entry_bytes < ScreenLayoutEntry_bytes || (kinds[kind].layout && !title))
        return NULL;
    uint32_t scratch_bytes = slst_bytes + 16 > 1024 ? slst_bytes + 16 : 1024;
    struct pane *p = Heap_AllocateZeroed(sizeof *p);
    uint32_t id = p ? core_resource_take(0, IDS) : 0;
    uint8_t *scratch = id ? Heap_AllocateZeroed(scratch_bytes) : NULL;
    if (!scratch) {
        if (p)
            Heap_Free(p);
        return NULL;
    }
    uint32_t next = id + 1;
    struct core_writer w = {scratch, 0};
    struct layout_clone c = {m->stock, id, 0, 0, 0, m->template, m->template, NULL, 0};
    if (title) {
        c.stock = kinds[kind].layout;
        c.from = kinds[kind].template;
        c.title_view = RES_StatusBarWhite_Text;
        c.title = title;
    }
    struct view_sources views;
    int built = kind == PANE_ICON ? icon_pane(m, id, &c, &w) : picture_pane(kind, id, &c, &views);
    built = built && core_clone_layout(&c, &next, scratch, scratch_bytes) && next <= id + 4;
    if (!built) {
        Heap_Free(scratch);
        Heap_Free(p);
        return NULL;
    }
    core_resource_set(Resource_SEVT, id, &none, sizeof none);
    for (uint32_t i = 0; i < sizeof p->record / 4; i++)
        p->record[i] = core_word_at(entry, 4 * i);
    p->record[ScreenLayoutEntry_layout / 4] = id;
    /* The stock SLst with one more block: the clone's record. */
    w.at = 0;
    core_write_word(&w, core_blocks_count(slst) + 1);
    core_write_bytes(&w, (const uint8_t *)slst + 4, slst_bytes - 4);
    uint32_t begin = core_write_block_begin(&w, 0);
    core_write_bytes(&w, p->record, sizeof p->record);
    core_write_block_end(&w, begin);
    core_resource_set(Resource_SLst, m->screen, scratch, w.at);
    Heap_Free(scratch);
    /* Storing the SLst freed its previous copy, where every Layout the
     * controller on screen built at push keeps its record. The new copy keeps
     * the blocks at their offsets, so the records move to it. */
    const uint8_t *fresh = stock(Resource_SLst, m->screen, &slst_bytes);
    uint8_t *definition = definition_of(m, controller);
    struct core_block block;
    for (uint32_t i = 0; fresh && definition && core_block_at(fresh, i, &block); i++) {
        uint32_t layout_id = core_word_at(block.data, ScreenLayoutEntry_layout);
        void **slot = UIntPointerMap_GetOrInsert(definition + ScreenDefinition_layout_cache, &layout_id);
        if (*slot)
            RV_FIELD(*slot, Layout_screen_list_record, const void *) = block.data;
    }
    p->menu = (uint8_t)(m - menus);
    p->kind = (uint8_t)kind;
    p->layout = id;
    p->next = panes;
    panes = p;
    return p;
}

/* The menu's layout of a kind, built on first use. */
static struct pane *pane_for(const struct menu *m, uint32_t kind, void *controller)
{
    struct pane *p = pane_of(m, kind);
    return p ? p : build(m, kind, controller);
}

/* Adds the layout to the menu's running controller the way UI_LoadScreenLayouts
 * adds a screen's own at push: the controller's definition keeps a Layout per
 * id, freed with the controller, and navigator.SwitchLayout looks the id up
 * there. The lookup inserts an empty entry, which is filled here. 1 when the
 * controller knows the layout. */
static int attach(const struct pane *p, void *controller)
{
    uint8_t *definition = definition_of(&menus[p->menu], controller);
    if (!definition)
        return 0;
    void **slot = UIntPointerMap_GetOrInsert(definition + ScreenDefinition_layout_cache, &p->layout);
    if (*slot)
        return 1;
    uint8_t *layout = Runtime_OperatorNew(Layout_bytes);
    if (!layout)
        return 0;
    layout = UI_ConstructLayout(layout, p->layout, (uint8_t)p->record[ScreenLayoutEntry_kind / 4],
                                p->record);
    RV_FIELD(layout, Layout_screen, void *) = definition;
    *slot = layout;
    return 1;
}

const char *core_panes_layout_name(uint8_t root)
{
    const struct menu *m = menu_of(root);
    return m ? m->name : NULL;
}

void core_panes_layout_events(void *layout, uint32_t layout_id)
{
    uint32_t bytes;
    for (struct pane *p = panes; p; p = p->next) {
        const void *events =
            p->layout == layout_id ? stock(Resource_SEVT, menus[p->menu].stock, &bytes) : NULL;
        if (events)
            core_clone_read_events(layout, events, bytes);
    }
}

static void notify(const struct pane *p)
{
    for (uint32_t i = 0; i < kinds[p->kind].views; i++)
        core_screens_notify(SOURCES(p) + i);
}

/* The entry's pane icon now. */
static const struct rv_bitmap *icon_of(const struct rv_menu_entry *e,
                                       struct rv_extension_record *owner)
{
    if (!e->bitmap_for)
        return e->bitmap;
    struct rv_context ctx = {owner, NULL, NULL, NULL, -1, NULL};
    return e->bitmap_for(&ctx);
}

/* A transparent pixel for a pane without an icon: an image view keeps its
 * last bitmap when it is answered NULL. */
static const void *clear_bitmap(void)
{
    static uint32_t clear[(BMap_payload + 4) / 4];
    if (!clear[0]) {
        uint8_t *b = (uint8_t *)clear;
        *(uint16_t *)(b + BMap_format) = BMapFormat_BGRA8888;
        *(uint16_t *)(b + BMap_stride) = 4;
        *(uint16_t *)(b + BMap_depth) = 32;
        ((int32_t *)(b + BMap_bounds))[2] = ((int32_t *)(b + BMap_bounds))[3] = 1;
        *(uint32_t *)(b + BMap_payload_bytes) = 4;
    }
    return clear;
}

static const void *icon_box(void)
{
    return ResourceService_GetBitmap(UI_GetResourceService(), RES_SettingsPreview_Background_Image);
}

/* The box behind an icon, or the transparent pixel for a row without one. */
static const void *box_for(const struct rv_menu_entry *e, struct rv_extension_record *owner)
{
    return icon_of(e, owner) && !e->plain_icon ? icon_box() : clear_bitmap();
}

/* The pane kind a decoded picture asks for by its size, -1 for none. */
static int kind_of(const void *bmap)
{
    const int32_t *bounds = (const int32_t *)((const uint8_t *)bmap + BMap_bounds);
    for (int k = 0; k < PANE_KINDS; k++)
        if (bounds[2] - bounds[0] == kinds[k].height && bounds[3] - bounds[1] == kinds[k].width)
            return k;
    return -1;
}

void core_panes_show(uint8_t root, uint32_t item, void *controller)
{
    const struct menu *m = menu_of(root);
    struct rv_extension_record *owner;
    const struct rv_menu_entry *e = m ? core_menus_entry(item, &owner) : NULL;
    if (!e)
        return;
    const void *picture = core_bitmap_picture(owner, icon_of(e, owner));
    int kind = picture ? kind_of(picture) : -1;
    struct pane *p = kind >= 0 ? pane_for(m, (uint32_t)kind, controller) : NULL;
    if (!p)
        p = pane_for(m, m->kind, controller);
    if (p)
        p->current = item;
    /* Each stock highlight handler records its pane's kind here and the
     * controller re-runs the handler of the recorded kind when the artwork
     * service's model changes, which would switch the pane away from this row;
     * 0, the kind of the stock rows with a fixed pane, re-runs nothing. */
    if (media_menu(root))
        RV_FIELD(controller, MainMediaMenu_preview_kind, uint8_t) = 0;
    /* The binding's navigator.SwitchLayout runs after this handler and looks
     * the menu's pane name up then. A layout the controller does not know
     * would abort the switch, so the stock row's layout stands in when the
     * pane could not be built or added. */
    uint32_t layout = p && attach(p, controller) ? p->layout : m->stock;
    uint32_t name[CppString_bytes / 4], allocator = 0, length = 0;
    if (pointed[m - menus] != layout) {
        while (m->name[length])
            length++;
        CppString_InitBytes(name, m->name, length, &allocator);
        *ResourceNames_FindOrInsertSlot(ResourceNameMap, name) = layout;
        CppString_Destroy(name);
        pointed[m - menus] = layout;
    }
    if (p)
        notify(p);
}

void core_panes_screen_closed(struct core_screen *screen)
{
    if (core_lists_kind(screen->declaration->kind) &&
        list_pane == CORE_LIST_PANE_CAPTION(core_lists_screen_id(screen, 1)))
        list_pane = 0;
}

/* `current` outlives the menu screen, so a refresh while it is closed decodes
 * the last row's picture once for nobody. */
void core_panes_refresh(void)
{
    for (struct pane *p = panes; p; p = p->next)
        if (p->current)
            notify(p);
    if (list_pane)
        for (uint32_t i = 0; i < 4; i++)
            core_screens_notify(list_pane + i);
}

void core_panes_refresh_owner(struct rv_extension_record *owner)
{
    struct rv_extension_record *o;
    for (struct pane *p = panes; p; p = p->next)
        if (p->current && core_menus_entry(p->current, &o) && o == owner)
            notify(p);
}

/* The entry's text for a line role, "" for none. */
static const char *line_of(const struct rv_menu_entry *e, struct rv_extension_record *owner,
                           uint32_t role)
{
    struct rv_context ctx = {owner, NULL, NULL, NULL, -1, NULL};
    const char *text = NULL;
    if (role == CAPTION || role == CAPTION_ONLY)
        text = e->caption_text ? e->caption_text(&ctx) : e->caption;
    else if (role == LINE || role == LINE2)
        text = e->line_text ? e->line_text(&ctx, role - LINE) : e->lines[role - LINE];
    if (!text && (role == CAPTION || role == LABEL))
        text = core_row_label(&e->row, owner, NULL);
    return text ? text : "";
}

int core_panes_property(uint32_t type, uint32_t id, uint32_t out)
{
    const struct rv_menu_entry *e;
    struct rv_extension_record *owner;
    struct pane *p = panes;
    while (p && id - SOURCES(p) >= kinds[p->kind].views)
        p = p->next;
    if (!p || !(e = core_menus_entry(p->current, &owner)))
        return 0;
    uint32_t kind = p->kind, role = kinds[kind].roles[id - SOURCES(p)];
    if (role == PICTURE && type == Property_Bitmap) {
        const struct rv_bitmap *b = icon_of(e, owner);
        const void *picture = kind == PANE_ICON ? core_bitmap_icon(owner, b) : core_bitmap_picture(owner, b);
        if (picture && kind != PANE_ICON && kind_of(picture) != (int)kind)
            picture = NULL; /* the view would clip it: the caption stands alone */
        *(const void **)out = picture ? picture : clear_bitmap();
    } else if (role == BADGE && type == Property_Bitmap) {
        struct rv_context ctx = {owner, NULL, NULL, NULL, -1, NULL};
        const struct rv_bitmap *b = e->badge_for ? e->badge_for(&ctx) : e->badge;
        const void *badge = core_bitmap_badge(owner, b);
        *(const void **)out = badge ? badge : clear_bitmap();
    } else if (role == BOX && type == Property_Bitmap)
        *(const void **)out = box_for(e, owner);
    else if (role != PICTURE && role != BADGE && role != BOX && type == Property_String)
        *(const char **)out = line_of(e, owner, role);
    else
        return 0;
    return 1;
}

int core_panes_list_property(struct core_screen *s, uint32_t type, uint32_t id, uint32_t out)
{
    const struct rv_screen *d = s->declaration;
    if (!core_lists_kind(d->kind))
        return 0;
    uint32_t caption = CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1)), index;
    /* A list with a pane per row answers for the highlighted row. */
    const struct rv_pane *pane = NULL;
    if (core_lists_per_row_pane(d) && (index = core_indexed_selected()) < d->list.count)
        pane = &d->list.panes[index];
    struct rv_context ctx = {s->owner, NULL, NULL, NULL, -1, NULL};
    if (id == caption && type == Property_String && d->kind == RV_SCREEN_KIND_CHOICE &&
        (index = core_indexed_selected()) < d->choice.count)
        *(const char **)out = d->choice.labels[index];
    else if (id == caption && type == Property_String && pane) {
        const char *text = pane->caption_text ? pane->caption_text(&ctx) : pane->caption;
        *(const char **)out = text ? text : "";
    } else if (id == caption && type == Property_String)
        *(const char **)out = d->caption ? d->caption : "";
    else if ((id == caption + 1 || id == caption + 2) && type == Property_Bitmap && pane) {
        const void *icon =
            core_bitmap_icon(s->owner, pane->bitmap_for ? pane->bitmap_for(&ctx) : pane->bitmap);
        if (!icon)
            *(const void **)out = clear_bitmap();
        else
            *(const void **)out = id == caption + 1 ? icon : icon_box();
    } else if (id == caption + 1 && type == Property_Bitmap)
        *(const void **)out = core_bitmap_icon(s->owner, d->bitmap);
    else if (id == caption + 2 && type == Property_Bitmap)
        *(const void **)out = icon_box();
    else if (id == caption + 3 && type == Property_String && pane)
        *(const char **)out = core_row_label(&d->list.rows[index], s->owner, NULL);
    else if (id == caption + 3 && type == Property_String)
        *(const char **)out = d->title ? d->title : "";
    else
        return 0;
    return 1;
}

/* The highlight moved: the pane's caption, and with a pane per row its icon and
 * title, are read again. */
int core_panes_list_action(struct core_screen *s, uint32_t id, uint32_t argument)
{
    const struct rv_screen *d = s->declaration;
    if (!core_lists_kind(d->kind) || id != CORE_LIST_PANE_CAPTION(core_lists_screen_id(s, 1)))
        return 0;
    if (argument == ActionSupportQuery)
        return 1;
    core_screens_notify(id);
    if (core_lists_per_row_pane(d)) {
        list_pane = id;
        core_screens_notify(id + 1);
        core_screens_notify(id + 2);
        core_screens_notify(id + 3);
    }
    return 1;
}

void core_panes_init(void *resource_bank)
{
    bank = resource_bank;
}
