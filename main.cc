#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>

#include <getopt.h>

#include "ttyml.h"

namespace {

int print_version;
int print_help;

struct option long_options[] = {{"version", no_argument, &print_version, 1},
                                {"help", no_argument, &print_help, 1},
                                {nullptr, 0, nullptr, 0}};

}  // namespace

int main(int argc, char** argv) try {
  const char* program_name = (argc > 0) ? argv[0] : "ttyml";

  int i;
  while ((i = getopt_long(argc, argv, "", long_options, 0)) != -1) {
    switch (i) {
      case 0:
        break;

      case '?':
        std::cerr << "Try `" << program_name
                  << " --help' for more information\n";
        return EXIT_FAILURE;
    }
  }

  if (print_help) {
    std::cout << "Usage: " << program_name << " [OPTION]... URL\n"
              << "\n"
              << "      --help     display this help and exit\n"
              << "      --version  display version information\n"
              << "\n"
              << "Report bugs to <morten.hustveit@gmail.com>\n";
    return EXIT_SUCCESS;
  }

  if (print_version) {
    std::cout << PACKAGE_STRING << '\n';
    return EXIT_SUCCESS;
  }

  if (optind + 1 != argc) {
    std::cerr << "Usage: " << program_name << " [OPTION]... URL\n";
    return EXIT_FAILURE;
  }

  const char* url = argv[optind++];

  auto context = std::make_unique<ttyml::Context>(url);

  while (context && context->has_prompt()) {
    context = context->next_context();
  }

} catch (std::runtime_error& e) {
  std::cerr << "Fatal error: " << e.what() << '\n';
  return EXIT_FAILURE;
}
