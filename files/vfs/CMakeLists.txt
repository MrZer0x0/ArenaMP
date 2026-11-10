if (NOT DEFINED OPENMW_MYGUI_FILES_ROOT)
    return()
endif()

# Copy the fixed core textures into the build directory.
set(DDIRRELATIVE resources/vfs/textures)
set(TEXTURE_FILES
    textures/crosshair.dds
    textures/ownership.dds
    textures/sneak.dds
    textures/omw_menu_scroll_down.dds
    textures/omw_menu_scroll_up.dds
    textures/omw_menu_scroll_left.dds
    textures/omw_menu_scroll_right.dds
    textures/omw_menu_scroll_center_h.dds
    textures/omw_menu_scroll_center_v.dds
    textures/menu_arenamw.png
)
copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_MYGUI_FILES_ROOT} ${DDIRRELATIVE} "${TEXTURE_FILES}")

# Built-in animation, prop and interaction UI resources. The engine mounts
# resources/vfs as its first data directory, so these files are available
# without enabling a separate OpenMW content package.
file(GLOB_RECURSE BUILTIN_VFS_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    animations/*.kf
    animations/*.nif
    animations/*.yaml
    animations/*.txt
    meshes/*.nif
    meshes/e/*.nif
    sound/*.wav
    sound/*.mp3
    textures/abot/fp/*.dds
    textures/vfx/*.dds
    textures/*.dds
    textures/vfx/*.tga
    textures/exploot/food/*.tga
    textures/ui/arenamw/*.png
    l10n/arenamp/*.ini
    icons/inventoryextender/*.dds
)

# ArenaMP Y062: old compatibility copies of OSSC quickcast lived directly in
# animations/xbase_anim*. They are byte-for-byte duplicates of the canonical
# animations/arenamp/ossc assets and are discovered by Additional Animation
# Sources before ArenaMP's explicit loader. Do not package them: on normal
# xbase_anim the duplicate contains Toe0 tracks that the skeleton does not have,
# causing duplicate missing-bone warnings. Canonical ArenaMP assets below remain.
list(FILTER BUILTIN_VFS_FILES EXCLUDE REGEX
    "^animations/(xbase_anim/xquickcast\\.kf|xbase_anim_female/xquickcast_female\\.kf|xbase_animkna/xquickcastkna\\.kf|xbase_anim\\.1st/xquickcast\\.1st\\.kf)$")


# Core animation assets must be present on desktop and Android. The mobile
# builder deploys this generated resources tree, not a user's Data Files folder.
set(ARENAMP_CORE_ANIMATIONS
    animations/arenamp/mao/xbase_anim.kf
    animations/arenamp/mao/xbase_anim_female.kf
    animations/arenamp/reanimation/xbase_anim.1st/_xreanimationv1.kf
    animations/arenamp/reanimation/xbase_anim.1st/x2cchop.kf
    animations/arenamp/reanimation/xbase_anim.1st/xbowshoot.kf
    animations/arenamp/thirdperson_alt/xbase_anim/xone_h_attack.kf
    animations/arenamp/thirdperson_alt/xbase_anim/xtwo_h_attack.kf
    animations/arenamp/thirdperson_alt/xbase_anim/xmagic.kf
    animations/arenamp/thirdperson_alt/xbase_animkna/xone_h_attack.kf
    animations/arenamp/ossc/xbase_anim/xquickcast.kf
    animations/arenamp/ossc/xbase_anim_female/xquickcast_female.kf
    animations/arenamp/ossc/xbase_animkna/xquickcastkna.kf
    animations/arenamp/ossc/xbase_anim.1st/xquickcast.1st.kf
)
foreach (f ${ARENAMP_CORE_ANIMATIONS})
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
        message(FATAL_ERROR "Missing ArenaMP core animation: ${f}")
    endif()
endforeach()

# Quest Manager icons use one canonical lowercase runtime path. Android/Linux
# assets are case-sensitive, while Windows source lookups are not, so validate
# the canonical files themselves instead of probing a differently-cased alias.
set(QUESTMAN_ICON_DIR "icons/questman")
# Do not probe a differently-cased legacy path here: Windows filesystems are
# case-insensitive, so EXISTS("Icons/questman") also matches the canonical
# "icons/questman" directory and would make every Windows configure fail.
# The per-file checks below are sufficient: on case-sensitive Android/Linux
# builds they require the canonical lowercase source path, while all copied
# runtime destinations remain lowercase on every platform.

# Keep the filenames explicit so generated IDE projects see every resource and a
# missing icon fails at configure time instead of becoming an in-game blank icon.
set(QUESTMAN_ICON_NAMES
    addon_bloodmoon.dds
    addon_cyrodiil.dds
    addon_morrowind.dds
    addon_skyrim.dds
    addon_tamriel.dds
    addon_tribunal.dds
    cat_abecette.dds
    cat_bounty.dds
    cat_daedric.dds
    cat_misc.dds
    cat_narsis_arena.dds
    cat_vampires.dds
    fa_anvil.dds
    fa_eec.dds
    fa_fighters.dds
    fa_imp_cult.dds
    fa_imp_legion.dds
    fa_janatta.dds
    fa_mages.dds
    fa_morag_tong.dds
    fa_priests.dds
    fa_shared.dds
    fa_temple.dds
    fa_thieves.dds
    gh_hlalu.dds
    gh_redoran.dds
    gh_shared.dds
    gh_telvanni.dds
    ui_check_mark.dds
    ui_completed.dds
    ui_filter.dds
    ui_hidden.dds
    ui_in_progress.dds
    ui_star.dds
)
set(QUESTMAN_ICON_FILES)
foreach (name ${QUESTMAN_ICON_NAMES})
    set(f "${QUESTMAN_ICON_DIR}/${name}")
    if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
        message(FATAL_ERROR "Missing Quest Manager Android-safe resource: ${f}")
    endif()
    list(APPEND QUESTMAN_ICON_FILES "${f}")
endforeach()
list(APPEND BUILTIN_VFS_FILES ${QUESTMAN_ICON_FILES})

# Consuming Animated pipe/smoke models reference these texture names directly.
# Keep them at the VFS texture root to preserve the paths embedded in the NIFs.
list(APPEND BUILTIN_VFS_FILES
    "textures/cigar logo.dds"
    "textures/cigar wrap.dds"
    "textures/metal_30w.dds"
    "textures/pc_misc_pipe_cob.dds"
)

# Cached incremental builds may contain an older reduced set. Clear only the
# generated directories owned by this source tree before copying the full set.
file(REMOVE_RECURSE "${OPENMW_MYGUI_FILES_ROOT}/resources/vfs/animations")
file(REMOVE_RECURSE "${OPENMW_MYGUI_FILES_ROOT}/resources/vfs/meshes/InteractionsAnimated")
foreach (f ${BUILTIN_VFS_FILES})
    copy_resource_file("${CMAKE_CURRENT_SOURCE_DIR}/${f}" "${OPENMW_MYGUI_FILES_ROOT}" "resources/vfs/${f}")
endforeach()
