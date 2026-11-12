# Test-only: inserts the profiler zones (prof.h) into a copy of the Linux port. Argument: <copy>/alpha
import sys
d = sys.argv[1]; hdr = sys.argv[2]
def patch(path, reps, prefix=''):
    s = open(path).read()
    for a, b in reps:
        n = s.count(a)
        assert n == 1, (path, n, a[:90])
        s = s.replace(a, b)
    open(path, 'w').write(prefix + s)
R = d + '/src/cube/GameControllerRender.cpp'
clock = 'gameTime()' if 'int startTime = gameTime();' in open(R).read() else 'timeGetTime()'
patch(R, [
 ('    int startTime = %s;\n' % clock, '    int startTime = %s;\n    PZ(SETUP);\n' % clock),
 ('    int activeSide = activeViewSide();\n', '    PZ(CHUNKSCAN);\n    int activeSide = activeViewSide();\n'),
 ('            Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];\n            if (pending[chunk->remeshTime] == 0) {',
  '            Chunk *chunk = &m_chunks[(y % m_viewDistance) * m_viewDistance + x % m_viewDistance];\n            PCOUNT(ACTIVE_CHUNKS, 1);\n            if (pending[chunk->remeshTime] == 0) {'),
 ('                visibleChunks.push_back(chunk);\n', '                visibleChunks.push_back(chunk);\n                PCOUNT(VISIBLE_CHUNKS, 1);\n'),
 ('            int phase = 0;\n', '            int phase = 0;\n            PCOUNT(PLANTS_SCANNED, chunk->plants.size());\n'),
 ('    // While "Please wait..." shows', '    PZ(UI);\n    // While "Please wait..." shows'),
 ('    // The sky and the light: bluish under water.', '    PZ(SETUP);\n    // The sky and the light: bluish under water.'),
 ('    // The lights of the frame: the magic projectiles', '    PZ(LIGHTS);\n    // The lights of the frame: the magic projectiles'),
 ('    int lightCount = std::min((int)lights.size(), 16);', '    PCOUNT(LIGHTS, lights.size());\n    int lightCount = std::min((int)lights.size(), 16);'),
 ('    // The clouds, where the land is humid', '    PZ(CLOUDS);\n    // The clouds, where the land is humid'),
 ('    // The chunks, nearest first, each with the point lights that reach it.', '    PZ(TERRAIN);\n    // The chunks, nearest first, each with the point lights that reach it.'),
 ('        Chunk *chunk = *it;\n        EnterCriticalSection(&chunk->lock);\n        if (!chunk->chunkBuffers.empty()) {',
  '        Chunk *chunk = *it;\n        PZ(TERRAIN);\n        EnterCriticalSection(&chunk->lock);\n        if (!chunk->chunkBuffers.empty()) {'),
 ('                m_device->SetStreamSource(0, (*buffer)->vertexBuffer, 0, sizeof(Vertex));\n                m_device->SetIndices((*buffer)->indexBuffer);',
  '                PCOUNT(CHUNK_BUFFERS, 1);\n                m_device->SetStreamSource(0, (*buffer)->vertexBuffer, 0, sizeof(Vertex));\n                m_device->SetIndices((*buffer)->indexBuffer);'),
 ('            // The chunk\'s plants, near enough.', '            PZ(PLANTS);\n            // The chunk\'s plants, near enough.'),
 ('                    renderPlant(*plant, distanceSquared, plantFadeEnd, plantFadeStart, index, view, m_projectionMatrix,',
  '                    PCOUNT(PLANTS_DRAWN, 1);\n                    renderPlant(*plant, distanceSquared, plantFadeEnd, plantFadeStart, index, view, m_projectionMatrix,'),
 ('    m_shader->setPointLights(pointLightPositions, pointLightColors);\n\n    // The items on the ground and the objects of the zones around the player.',
  '    PZ(ITEMS);\n    m_shader->setPointLights(pointLightPositions, pointLightColors);\n\n    // The items on the ground and the objects of the zones around the player.'),
 ('    // The creatures, farthest first (none behind the start menu\'s screens).', '    PZ(CREATURES);\n    // The creatures, farthest first (none behind the start menu\'s screens).'),
 ('        creatures.push_back(entry);\n', '        creatures.push_back(entry);\n        PCOUNT(CREATURES, 1);\n'),
 ('        // The animation of the ability in progress, in the light of the block below the creature.',
  '        PCOUNT(CREATURES_DRAWN, 1);\n        // The animation of the ability in progress, in the light of the block below the creature.'),
 ('    // A debugging overlay (m_showPathSearch', '    PZ(EFFECTS);\n    // A debugging overlay (m_showPathSearch'),
 ('    // The weapon trails of the creatures attacking in melee', '    PZ(CREATURES);\n    // The weapon trails of the creatures attacking in melee'),
 ('    // The water faces of the chunks, farthest first.', '    PZ(WATER);\n    // The water faces of the chunks, farthest first.'),
 ('    // The plants and objects beyond their fade distance, farthest first.', '    PZ(DEFERRED);\n    PCOUNT(DEFERRED, deferred.size());\n    // The plants and objects beyond their fade distance, farthest first.'),
 ('    // The creatures in stealth: their depth first', '    PZ(CREATURES);\n    // The creatures in stealth: their depth first'),
 ('    // The interface: the minimap and the ring menu', '    PZ(UI);\n    // The interface: the minimap and the ring menu'),
], '#include "%s"\n' % hdr)
G = d + '/src/cube/GameController.cpp'
patch(G, [
 ('        while (step > 20) {\n            m_world.update(20, &updates, 0);\n            step -= 20;\n        }\n        m_world.update(step, &updates, 0);\n',
  '        PZ(WORLD);\n        while (step > 20) {\n            m_world.update(20, &updates, 0);\n            step -= 20;\n        }\n        m_world.update(step, &updates, 0);\n        PZ(UPDATE);\n'),
], '#include "%s"\n' % hdr)
M = d + '/platform/linux/main.cpp'
patch(M, [
 ('    static DWORD s_lastTime; // Previous frame time.\n', '    static DWORD s_lastTime; // Previous frame time.\n    profFrameBoundary(g_device, (int)g_game->m_world.m_creatures.size());\n'),
 ('    g_game->update(now - s_lastTime);\n', '    PZ(UPDATE);\n    g_game->update(now - s_lastTime);\n    PZ(OTHER);\n'),
 ('    g_game->render();\n    if (g_device->Present(0, 0, 0, 0) == D3DERR_DEVICELOST)\n        g_deviceLost = true;\n',
  '    g_game->render();\n    PZ(PRESENT);\n    if (g_device->Present(0, 0, 0, 0) == D3DERR_DEVICELOST)\n        g_deviceLost = true;\n    PZ(OTHER);\n'),
 ('    SDL_StopTextInput(g_window);\n', '    profWriteSamples();\n    SDL_StopTextInput(g_window);\n'),
], '#include "%s"\n' % hdr)
print('ok')
