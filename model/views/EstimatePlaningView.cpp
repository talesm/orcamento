#include "EstimatePlaningView.h"

static auto sql = R"=(
SELECT 
    estim.estimate_id,
    estim.name AS name,
    DATE(bud.start, estim.due) AS due,
    estim.amount/100 AS estimated,
    cat.name AS category, 
    estim.obs AS obs
FROM budget bud
    JOIN estimate estim USING(budget_id)
    JOIN category cat USING(category_id)
  WHERE budget_id = ?1
)=";

EstimatePlaningView::EstimatePlaningView():
    OrcaView(std::string(sql)+"\nORDER BY category_id, estim.name")
{
    //ctor
}

void EstimatePlaningView::setup(SQLite::Statement& stm)
{
    stm.bind(1, _budgetId);
    for(size_t i = 0; i < _params.sValues.size(); ++i){
        std::stringstream ss;
        ss << ":s_" << (i+1);
        stm.bind(ss.str(), _params.sValues[i]);
    }
    for(size_t i = 0; i < _params.iValues.size(); ++i){
        std::stringstream ss;
        ss << ":i_" << (i+1);
        stm.bind(ss.str(), _params.iValues[i]);
    }
}
void EstimatePlaningView::search(const Search& search)
{
    if(!search){
        _params = SearchQuery{};
        return query(sql);
    }
    _params = sqlize(search, { 
        { "name", "estim.name" },
        { "obs", "estim.obs" },
        { "estimated", "estim.amount", FieldDescriptor::MONEY },
        { "due", "CAST(STRFTIME('%d', \"start\", due) AS INTEGER)", FieldDescriptor::INT },
    });
    if(!_params.query.size()){
        return query(sql + std::string("\nORDER BY category_id, estim.name"));
    }
    query(std::string(sql) + " AND " + _params.query + "\nORDER BY category_id, estim.name");
}

