# bash completion for lsfg-vk
# SPDX-License-Identifier: GPL-3.0-or-later

_lsfg_vk() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    local commands="config doctor run validate benchmark debug"

    if [ "${COMP_CWORD}" -eq 1 ]; then
        mapfile -t COMPREPLY < <(compgen -W "${commands}" -- "${cur}")
        return
    fi

    case "${COMP_WORDS[1]}" in
        config)
            if [ "${COMP_CWORD}" -eq 2 ]; then
                mapfile -t COMPREPLY < <(compgen -W "list show set create delete game global --json" -- "${cur}")
            elif [ "${COMP_WORDS[2]}" = "set" ] && [ "${COMP_CWORD}" -eq 4 ]; then
                mapfile -t COMPREPLY < <(compgen -W "multiplier target-fps flow-scale performance-mode pacing gpu name" -- "${cur}")
            elif [ "${COMP_WORDS[2]}" = "global" ] && [ "${COMP_CWORD}" -eq 3 ]; then
                mapfile -t COMPREPLY < <(compgen -W "dll allow-fp16" -- "${cur}")
            elif [ "${COMP_WORDS[2]}" = "game" ] && [ "${COMP_CWORD}" -eq 3 ]; then
                mapfile -t COMPREPLY < <(compgen -W "add rm" -- "${cur}")
            fi
            ;;
        run)
            mapfile -t COMPREPLY < <(compgen -W "--multiplier --target-fps --flow-scale --performance-mode --pacing --gpu --dll --" -- "${cur}")
            ;;
        validate)
            mapfile -t COMPREPLY < <(compgen -W "--config" -- "${cur}")
            ;;
    esac
}

complete -F _lsfg_vk lsfg-vk
complete -F _lsfg_vk lsfg-vk-cli
