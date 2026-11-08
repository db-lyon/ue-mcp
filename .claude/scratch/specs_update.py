"""Apply this branch's C++ spec changes to tests/golden/handler-specs.json.

The file is normally recorded from a running editor (npm run specs:record);
this mirrors the RegisterHandler edits exactly so the TS surface can be
regenerated without one. A later re-record must produce no diff.
"""
import json, collections

path = 'tests/golden/handler-specs.json'
doc = json.load(open(path, encoding='utf-8'), object_pairs_hook=collections.OrderedDict)
H = doc['handlers']

def P(name, typ, desc, required=False, **extra):
    d = collections.OrderedDict([('name', name), ('type', typ), ('required', required), ('description', desc)])
    for k, v in extra.items():
        d[k] = v
    return d

def find(handler, name):
    for p in H[handler]['params']:
        if p['name'] == name:
            return p
    raise KeyError(f'{handler}.{name}')

SHARED = [
    P('label', 'string', 'Actor label; an existing actor with this label is reported rather than duplicated'),
    P('onConflict', 'string', 'When the label or name is taken: skip (default) | error'),
    P('location', 'vec3', 'World location'),
    P('rotation', 'rotator', 'World rotation'),
    P('scale', 'vec3', 'Actor scale'),
    P('folderPath', 'string', 'World Outliner folder'),
    P('world', 'string', 'World scope: editor (default) | pie'),
    P('pieInstance', 'integer', 'Which PIE world when world is pie: 0 = server or primary, 1..N = clients'),
]

def spawn(handler, own, pie='integer'):
    names = {p['name'] for p in own}
    shared = [dict(p, type=pie) if p['name'] == 'pieInstance' else p for p in SHARED]
    H[handler]['params'] = own + [collections.OrderedDict(p) for p in shared if p['name'] not in names]

# F6: every spawn takes place_actor's params.
spawn('place_actor', [find('place_actor', n) for n in ('actorClass', 'staticMesh', 'material')])
spawn('internal_spawn_actor', [find('internal_spawn_actor', n) for n in ('actorClass', 'label', 'staticMesh', 'material')])
spawn('spawn_light', [find('spawn_light', n) for n in ('lightType', 'intensity', 'color', 'mobility', 'attenuationRadius')])
spawn('spawn_volume', [
    P('volumeType', 'string', 'Volume class, by short name or alias such as trigger, blocking, postprocess, navmesh, pcg', True),
    P('extent', 'vec3', 'Half extent of the cube brush, positive on every axis (default 100, 100, 100)'),
    P('graphPath', 'string', 'PCG graph for a volume with a PCG component; refused when it does not load'),
])
skel = H['spawn_skeletal_mesh_actor']
own = [find('spawn_skeletal_mesh_actor', n) for n in ('skeletalMesh', 'meshPath', 'transform', 'materials', 'animSequence', 'loop')]
own[4] = P('animSequence', 'string', 'Single-node preview animation; refused when it does not load')
spawn('spawn_skeletal_mesh_actor', own)
spawn('spawn_nav_modifier_volume', [find('spawn_nav_modifier_volume', n) for n in ('extent', 'areaClass', 'scale')]
    + [P('graphPath', 'string', 'Shared with the other volume spawns; a NavModifierVolume has no PCG component, so it is refused')], 'number')
spawn('add_pcg_volume', [
    P('graphPath', 'string', "PCGGraph to assign to the volume's component; refused when it does not load"),
    P('extent', 'vec3', 'Half-size of the volume box {x,y,z}, positive on every axis (default 500 on each axis)'),
])
spawn('spawn_ambient_sound', [find('spawn_ambient_sound', n) for n in ('soundPath', 'volumeMultiplier')])

# delete_actor replays a spawn in the world the spawn named.
H['delete_actor']['params'] += [find('get_actor_details', 'world'), find('get_actor_details', 'pieInstance')]

# F4: the framework creators take onConflict like every other creator.
ON_CONFLICT_ASSET = P('onConflict', 'string', 'skip (default, returns the existing asset) or error when the asset already exists')
for h in ('create_game_mode', 'create_game_state', 'create_player_controller', 'create_player_state', 'create_hud'):
    ps = H[h]['params']
    i = [p['name'] for p in ps].index('packagePath')
    ps.insert(i + 1, ON_CONFLICT_ASSET)

cb = H['create_blueprint']
find('create_blueprint', 'packagePath')['description'] = 'Destination folder, used with name (default /Game/Blueprints)'
cb['choices'] = [collections.OrderedDict([('mode', 'exactlyOne'), ('branches', [['assetPath'], ['name']])])]

cbi = H['create_blueprint_interface']
ap = find('create_blueprint_interface', 'assetPath')
ap['required'] = False
oc = find('create_blueprint_interface', 'onConflict')
cbi['params'] = [ap,
    P('name', 'string', 'Asset name; with packagePath, the same destination as assetPath'),
    P('packagePath', 'string', 'Destination folder, used with name (default /Game/Blueprints)'),
    oc]
exempt = cbi.pop('contractExempt')
cbi['choices'] = [collections.OrderedDict([('mode', 'exactlyOne'), ('branches', [['assetPath'], ['name']])])]
cbi['contractExempt'] = exempt

# F6 component templates: a name like add_ability_system_component's.
H['add_state_tree_component']['params'].append(P('componentName', 'string', 'Name of the new component (default StateTreeComp)'))
H['add_smart_object_component']['params'].append(P('componentName', 'string', 'Name of the new component (default SmartObjectComp)'))

# F7: one world rule, reachable from every runtime action.
H['set_runtime_focus']['params'] += [
    P('world', 'string', 'Runtime world scope: pie (default) | game | auto'),
    P('pieInstance', 'integer', 'PIE instance whose widget takes focus'),
]
H['get_runtime_focus_path']['params'] += [
    P('world', 'string', 'Runtime world scope: pie (default) | game | auto'),
    P('pieInstance', 'integer', 'PIE instance that must be running'),
]
for h in ('set_pie_time_scale', 'get_pie_pawn', 'pie_set_player_view', 'stage_game_input'):
    H[h]['params'] += [
        P('world', 'string', 'pie (default) | game | auto'),
        P('pieInstance', 'number', 'PIE world instance; omit for the first running one'),
    ]
for h in ('get_bone_transform', 'list_bones', 'set_live_post_process_anim_blueprint', 'get_live_bone_transforms'):
    ps = H[h]['params']
    i = [p['name'] for p in ps].index('world')
    ps.insert(i + 1, P('pieInstance', 'number', 'PIE world instance; a live world only, never widened to another'))

with open(path, 'w', encoding='utf-8', newline='\n') as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
    f.write('\n')
print('updated')
