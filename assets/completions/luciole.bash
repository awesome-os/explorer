_luciole() {
  local cur="${COMP_WORDS[COMP_CWORD]}"
  if [[ $COMP_CWORD -eq 2 && ${COMP_WORDS[1]} == completions ]]; then
    COMPREPLY=( $(compgen -W 'bash zsh fish' -- "$cur") )
  elif [[ $COMP_CWORD -eq 2 && ${COMP_WORDS[1]} == config ]]; then
    COMPREPLY=( $(compgen -W 'validate' -- "$cur") )
  elif [[ $COMP_CWORD -eq 3 && ${COMP_WORDS[1]} == config && ${COMP_WORDS[2]} == validate ]]; then
    mapfile -t COMPREPLY < <(compgen -f -- "$cur")
    compopt -o filenames
  elif [[ $COMP_CWORD -eq 1 ]]; then
    mapfile -t COMPREPLY < <(
      compgen -W '-h --help -v --version completions config' -- "$cur"
      compgen -d -- "$cur"
    )
    compopt -o filenames
  else
    COMPREPLY=()
  fi
}
complete -F _luciole luciole
