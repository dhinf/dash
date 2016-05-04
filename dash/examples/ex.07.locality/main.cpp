#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <cstddef>
#include <sstream>

#include <libdash.h>

using namespace std;

std::ostream & operator<<(
  std::ostream           & os,
  dart_locality_scope_t    scope);

void print_domain(
  dart_team_t              team,
  dart_domain_locality_t * domain);

int main(int argc, char * argv[])
{
  pid_t pid;
  char buf[100];

  dash::init(&argc, &argv);

  dart_barrier(DART_TEAM_ALL);
  sleep(5);

  auto myid = dash::myid();
  auto size = dash::size();

  gethostname(buf, 100);
  pid = getpid();

  // To prevent interleaving output:
  std::ostringstream i_os;
  i_os << "Process started at "
       << "unit " << setw(3) << myid << " of "  << size << " "
       << "on "   << buf     << " pid:" << pid
       << endl;
  cout << i_os.str();

  dart_barrier(DART_TEAM_ALL);
  sleep(5);

  if (myid == 0) {
    dart_domain_locality_t * global_domain_locality;
    dart_domain_locality(DART_TEAM_ALL, ".", &global_domain_locality);
    print_domain(DART_TEAM_ALL, global_domain_locality);
  } else {
    sleep(5);
  }

  auto & split_team = dash::Team::All().split(2);

  std::ostringstream t_os;
  t_os << "Unit id " << setw(3) << myid << " -> "
       << "unit id " << setw(3) << split_team.myid() << " "
       << "in team " << split_team.dart_id() << " after split"
       << endl;
  cout << t_os.str();

  dart_barrier(DART_TEAM_ALL);
  sleep(1);

  if (split_team.dart_id() == 1 && split_team.myid() == 0) {
    dart_domain_locality_t * global_domain_locality;
    dart_domain_locality(split_team.dart_id(), ".", &global_domain_locality);
    print_domain(split_team.dart_id(), global_domain_locality);
  } else {
    sleep(2);
  }
  dart_barrier(DART_TEAM_ALL);

  if (split_team.dart_id() == 2 && split_team.myid() == 0) {
    dart_domain_locality_t * global_domain_locality;
    dart_domain_locality(split_team.dart_id(), ".", &global_domain_locality);
    print_domain(split_team.dart_id(), global_domain_locality);
  } else {
    sleep(2);
  }
  dart_barrier(DART_TEAM_ALL);

#if 0
  for (dart_unit_t u = 0; u < static_cast<dart_unit_t>(size); ++u) {
    if (u == myid) {
      // To prevent interleaving output:
      std::ostringstream     u_os;
      dart_unit_locality_t * uloc;
      dart_unit_locality(DART_TEAM_ALL, u, &uloc);
      u_os << "unit " << u << " locality: " << endl
           << "  unit:      " << uloc->unit               << endl
           << "  host:      " << uloc->host               << endl
           << "  domain:    " << uloc->domain_tag         << endl
           << "  numa_id:   " << uloc->hwinfo.numa_id     << endl
           << "  core_id:   " << uloc->hwinfo.cpu_id      << endl
           << "  num_cores: " << uloc->hwinfo.num_cores   << endl
           << "  cpu_mhz:   " << uloc->hwinfo.min_cpu_mhz << "..."
                              << uloc->hwinfo.max_cpu_mhz << endl
           << "  threads:   " << uloc->hwinfo.min_threads << "..."
                              << uloc->hwinfo.max_threads
           << endl;
      cout << u_os.str();
    }
    dart_barrier(DART_TEAM_ALL);
  }
#endif
  // To prevent interleaving output:
  std::ostringstream f_os;
  f_os << "Process exiting at "
       << "unit " << setw(3) << myid << " of "  << size << " "
       << "on "   << buf     << " pid:" << pid
       << endl;
  cout << f_os.str();
  dash::finalize();

  return EXIT_SUCCESS;
}

std::ostream & operator<<(
  std::ostream          & os,
  dart_locality_scope_t   scope)
{
  switch(scope) {
    case DART_LOCALITY_SCOPE_GLOBAL: os << "GLOBAL";    break;
    case DART_LOCALITY_SCOPE_NODE:   os << "NODE";      break;
    case DART_LOCALITY_SCOPE_MODULE: os << "MODULE";    break;
    case DART_LOCALITY_SCOPE_NUMA:   os << "NUMA";      break;
    case DART_LOCALITY_SCOPE_UNIT:   os << "UNIT";      break;
    case DART_LOCALITY_SCOPE_CORE:   os << "CORE";      break;
    default:                         os << "UNDEFINED"; break;
  }
  return os;
}

void print_domain(
  dart_team_t              team,
  dart_domain_locality_t * domain)
{
  const int max_level = 3;

  if (domain->level > max_level) {
    return;
  }
  std::string indent(domain->level * 4, ' ');

  cout << indent << "level:  " << domain->level
       << endl
       << indent << "scope:  " << domain->scope
       << endl
       << indent << "domain: " << domain->domain_tag
       << endl;

  if (domain->level == 0) {
    cout << indent << "nodes:  " << domain->num_nodes << endl;
  } else {
    cout << indent << "host:   " << domain->host << endl;
  }
  if (domain->num_units > 0) {
    cout << indent << "- units: " << domain->num_units << endl;
    if (domain->level == 3) {
      for (int u = 0; u < domain->num_units; ++u) {
        dart_unit_t            unit_id = domain->unit_ids[u];
        dart_unit_locality_t * uloc;
        dart_unit_locality(team, unit_id, &uloc);
        cout << indent << "  units[" << setw(3) << u << "]: " << unit_id
                       << endl;
        cout << indent << "              unit_g: " << uloc->unit
                       << endl;
        cout << indent << "              team:   " << uloc->team
                       << endl;
        cout << indent << "              unit_l: " << uloc->team_unit
                       << endl;
        cout << indent << "              host:   " << uloc->host
                       << endl;
        cout << indent << "              domain: " << uloc->domain_tag
                       << endl;
        cout << indent << "              hwinfo: "
                       << "numa_id: " << uloc->hwinfo.numa_id << " "
                       << "cpu_id: "  << uloc->hwinfo.cpu_id  << " "
                       << "threads: " << uloc->hwinfo.min_threads << "..."
                                      << uloc->hwinfo.max_threads << " "
                       << "cpu_mhz: " << uloc->hwinfo.min_cpu_mhz << "..."
                                      << uloc->hwinfo.max_cpu_mhz
                       << endl;
      }
    }
  }
  if (domain->level <= max_level && domain->num_domains > 0) {
    cout << indent << "- domains: " << domain->num_domains << endl;
    for (int d = 0; d < domain->num_domains; ++d) {
      cout << indent << "  domains[" << d << "]: " << endl;
      print_domain(team, &domain->domains[d]);
    }
  }
}
