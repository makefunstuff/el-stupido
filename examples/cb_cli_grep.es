use cli
name "greptool"
desc "search files for patterns"
flag verbose -v "enable verbose output"
flag count -c "show count only"
arg pattern "regex pattern"
arg file "file to search"

fn cli_main(cli: *Cli) {
  if cli.verbose { printf("searching %s for '%s'\n", cli.file, cli.pattern) }
  printf("pattern: %s\nfile: %s\n", cli.pattern, cli.file)
  if cli.count { printf("(count mode)\n") }
}
