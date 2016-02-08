#include "MainControllerSqlite0_5.hpp"

using namespace std;
namespace orca
{
MainControllerSqlite0_5::MainControllerSqlite0_5(const string &name)
    : a_db(name, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
{
  SQLite::Transaction transaction(a_db);
  a_db.exec(
      "CREATE TABLE IF NOT EXISTS \"meta\" ("
      "  \"key\"           VARCHAR PRIMARY KEY,"
      "  \"value\"         TEXT"
      ");"
      "INSERT OR IGNORE INTO meta VALUES('format-name', 'orcamento'),"
      "  ('format-version', '0.5.0');");
  transaction.commit();
}

} /* orca */

vector<string> orca::MainControllerSqlite0_5::listBudgets() const { return {"stub"}; }

void orca::MainControllerSqlite0_5::flush()
{
  //Should commit...
}
