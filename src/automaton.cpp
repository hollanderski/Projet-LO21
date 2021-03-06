#include "automaton.h"
#include <stdexcept>

const std::vector<std::string> explode(const std::string& s, const char& c)
{
    std::string buff{""};
    std::vector<std::string> v;

    for(auto n:s)
    {
        if(n != c) buff+=n; else
        if(n == c && buff != "") { v.push_back(buff); buff = ""; }
    }
    if(buff != "") v.push_back(buff);

    return v;
}

Automaton::Automaton(const unsigned int id, sqlite3 * db) {
    std::ostringstream req1, req2;
    req1 << "SELECT value FROM automata WHERE id = " << id;
    sqlite3_exec(db, req1.str().c_str(), callback_load_automata, this, nullptr);
    req2 << "UPDATE automata SET lastUse = date('now') WHERE id = " << id;
    sqlite3_exec(db, req2.str().c_str(), nullptr, nullptr, nullptr);
}

Automaton::Automaton(QString const& fileName) {
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return;
    QTextStream in(&file);
    QString value = in.readLine();
    deserialize(value.toStdString());
}

static int callback_load_automata(void *ptr, int count, char **data, char **columns) {
    Automaton * auPtr = static_cast<Automaton *>(ptr);
    auPtr->deserialize(std::string(data[0]));
    return 0;
}

void Automaton::deserialize(const std::string &s) {
    std::vector<std::string> v = explode(s, '|');
    n = atoi(v[0].c_str());
    dim = atoi(v[1].c_str());
    defaultNext = v[2].c_str()[0];
    deserializeNbRules(v[3]);
    ruleBst = new RuleBst(v[4]);
}

char Automaton::next(std::string s) {
    if (s.length() != n) throw std::invalid_argument("wrong string size");
    ruleBst->reset();
    bool inTree = true;
    unsigned int nb_neigh = 0;

    char state;
    for(const char& c : s) { // neighbour counting + tree search
        if (c == '1') nb_neigh++;

        state = ruleBst->next(c); 
        if (inTree && state == 'n') { // not in the tree anymore
            ruleBst->reset();
            inTree = false;
        }
        else if (inTree && state != c) { // leaf
            return state;
        }
    }

    // The current cell isn't a neighboor of itself
    if (s[n/2] == '1') nb_neigh--;

    for(const Range& r : ruleNbNeighbLife) { // Life number rules
        if (r.a <= nb_neigh && nb_neigh <= r.b) return 'a'; 
    }
    for(const Range& r : ruleNbNeighbDeath) { // Death number rules
        if (r.a <= nb_neigh && nb_neigh <= r.b) return 'd'; 
    }
    for(const Range& r : ruleNbNeighbSame) { // Same number rules
        if (r.a <= nb_neigh && nb_neigh <= r.b) return 's'; 
    }

    // if nothing is matched return the default value
    return defaultNext;
}


void Automaton::insertRangeInto(std::vector<Range>& coll, unsigned int a, unsigned int b) {
    coll.push_back(Range {a, b});
}


std::string Automaton::serializeNbRules() {
    std::string seq = "";
    for(Range& r : ruleNbNeighbLife) seq += std::to_string(r.a)+","+std::to_string(r.b)+",";
    seq.push_back(';');
    for(Range& r : ruleNbNeighbDeath) seq += std::to_string(r.a)+","+std::to_string(r.b)+",";
    seq.push_back(';');
    for(Range& r : ruleNbNeighbSame) seq += std::to_string(r.a)+","+std::to_string(r.b)+",";

    return seq;
}


void Automaton::deserializeNbRules(const std::string& s) {
    ruleNbNeighbLife.clear();
    ruleNbNeighbDeath.clear();
    ruleNbNeighbSame.clear();

    std::string a = "";
    std::string b = "";
    char tabType = 'l';  // the save in nbLife

    bool isa = true, isb = false;
    for(const char& c : s) {
        if (c == ',' && isa) {
            isa = false;
            isb = true;
            continue;
        }
        if (c == ',' && isb) { // end of a range, insert
            isa = true;
            isb = false;
            
            // append the new range to the right container
            if (tabType == 'l') ruleNbNeighbLife.push_back(Range {
                    static_cast<unsigned int>(std::stoul(a)),
                    static_cast<unsigned int>(std::stoul(b))
                    });
            else if (tabType == 'd') ruleNbNeighbDeath.push_back(Range {
                    static_cast<unsigned int>(std::stoul(a)),
                    static_cast<unsigned int>(std::stoul(b))
                    });
            else ruleNbNeighbSame.push_back(Range {
                    static_cast<unsigned int>(std::stoul(a)),
                    static_cast<unsigned int>(std::stoul(b))
                    });

            // reset a and b
            a.clear(); b.clear();
            continue;
        }
        if (c == ';') { // start of ruleNbNeighbDeath
            isa = true;
            isb = false;
            tabType = tabType == 'l' ? 'd' : 's';
            continue;
        }

        // append the current char to the right string
        if (isa) a.push_back(c);
        if (isb) b.push_back(c);
    }
}

std::string Automaton::serialize() {
    std::ostringstream flux;
    flux << n << "|" << dim << "|" << defaultNext << "|" << serializeNbRules() << "|" << ruleBst->serialize();
    return flux.str();
}

unsigned int Automaton::save(const QString& name, sqlite3 * db) {
    std::ostringstream flux;
    flux << "INSERT INTO automata(name, is2d, value, lastUse) VALUES('";
    flux << name.toStdString() << "', " << (dim == 2 ? "true" : "false") << ", '" << serialize() << "', " << "date('now'))";
    sqlite3_exec(db, flux.str().c_str(), nullptr,nullptr,nullptr);
    unsigned int * ptr = new unsigned int;
    sqlite3_exec(db, "SELECT id FROM automata WHERE id=@@Identity", callback_get_id_automata, ptr, nullptr);
    return *ptr;
}

static int callback_get_id_automata(void *ptr, int count, char **data, char **columns) {
    unsigned int * intPtr = static_cast<unsigned int  *>(ptr);
    *intPtr = atoi(data[0]);
    return 0;
}

void Automaton::exportToFile(QString const& name) {
    QFile file(name);
    file.open(QIODevice::WriteOnly);
    QTextStream out(&file);
    out << QString(serialize().c_str());
}
