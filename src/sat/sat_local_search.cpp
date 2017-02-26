/*++
  Copyright (c) 2017 Microsoft Corporation

  Module Name:

  sat_local_search.cpp

  Abstract:
   
  Local search module for cardinality clauses.

  Author:

  Sixue Liu 2017-2-21

  Notes:

  --*/

#include "sat_local_search.h"
#include "sat_solver.h"
#include "card_extension.h"
#include "sat_params.hpp"

namespace sat {

    void local_search::init() {

        best_solution.resize(num_vars() + 1, false);
        constraint_slack.resize(num_constraints(), 0);
        cur_solution.resize(num_vars() + 1, false);
        m_index_in_unsat_stack.resize(num_constraints(), 0);
        coefficient_in_ob_constraint.resize(num_vars() + 1, 0);
        var_neighbor.reset();

        // for dummy var 0
        var_neighbor.push_back(bool_var_vector());

        for (bool_var v = 1; v <= num_vars(); ++v) {
            bool_vector is_neighbor(num_vars() + 1, false);
            var_neighbor.push_back(bool_var_vector());
            for (unsigned i = 0; i < var_term[v].size(); ++i) {
                unsigned c = var_term[v][i].constraint_id;
                for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                    bool_var w = constraint_term[c][j].var_id; 
                    if (w == v || is_neighbor[w]) continue;
                    is_neighbor[w] = true;
                    var_neighbor.back().push_back(w);
                }
            }
        }
        for (unsigned i = 0; i < ob_constraint.size(); ++i)
            coefficient_in_ob_constraint[ob_constraint[i].var_id] = ob_constraint[i].coefficient;

        set_parameters();
    }

    void local_search::reinit() {
        reinit_orig();
    }

    void local_search::init_cur_solution() {
        //cur_solution.resize(num_vars() + 1, false);
        for (unsigned v = 1; v <= num_vars(); ++v) {
            cur_solution[v] = (rand() % 2 == 1);
        }
    }

    // figure out slack, and init unsat stack
    void local_search::init_slack() {
        for (unsigned c = 0; c < num_constraints(); ++c) {
            for (unsigned i = 0; i < constraint_term[c].size(); ++i) {
                unsigned v = constraint_term[c][i].var_id;
                if (cur_solution[v] == constraint_term[c][i].sense)
                    --constraint_slack[c];
            }
            // constraint_slack[c] = constraint_k[c] - true_terms_count[c];
            // violate the at-most-k constraint
            if (constraint_slack[c] < 0)
                unsat(c);
        }
    }

    // figure out variables scores and sscores
    void local_search::init_scores() {
        for (unsigned v = 1; v <= num_vars(); ++v) {
            for (unsigned i = 0; i < var_term[v].size(); ++i) {
                int c = var_term[v][i].constraint_id;
                if (cur_solution[v] != var_term[v][i].sense) {
                    // will ++true_terms_count[c]
                    // will --slack
                    if (constraint_slack[c] <= 0) {
                        --sscore[v];
                        if (constraint_slack[c] == 0)
                            --m_var_info[v].m_score;
                    }
                }
                else { // if (cur_solution[v] == var_term[v][i].sense)
                    // will --true_terms_count[c]
                    // will ++slack
                    if (constraint_slack[c] <= -1) {
                        ++sscore[v];
                        if (constraint_slack[c] == -1)
                            ++m_var_info[v].m_score;
                    }
                }
            }
        }
    }

    // init goodvars 
    void local_search::init_goodvars() {
        goodvar_stack.reset();
        for (unsigned v = 1; v <= num_vars(); ++v) {
            if (score(v) > 0) { // && conf_change[v] == true
                m_var_info[v].m_in_goodvar_stack = true;
                goodvar_stack.push_back(v);
            }
        }
    }

    void local_search::reinit_orig() {
        constraint_slack = constraint_k;
        
        // init unsat stack
        m_unsat_stack.reset();
        
        // init solution: random now
        init_cur_solution();
        
        // init varibale information
        // variable 0 is the virtual variable

        m_var_info.reset(); 
        sscore.reset(); 
        time_stamp.reset(); 
        cscc.reset();

        m_var_info.resize(num_vars() + 1, var_info()); 
        m_var_info[0].m_score = INT_MIN;
        m_var_info[0].m_conf_change = false;
        sscore.resize(num_vars() + 1, 0);         sscore[0] = INT_MIN;
        time_stamp.resize(num_vars() + 1, 0);     time_stamp[0] = max_steps + 1;
        cscc.resize(num_vars() + 1, 1);           cscc[0] = 0;


        init_slack();
        init_scores();
        init_goodvars();

    }
    
    void local_search::calculate_and_update_ob() {
        unsigned i, v;
        objective_value = 0;
        for (i = 0; i < ob_constraint.size(); ++i) {
            v = ob_constraint[i].var_id;
            if (cur_solution[v])
                objective_value += ob_constraint[i].coefficient;
        }
        if (objective_value > best_objective_value) {
            best_solution = cur_solution;
            best_objective_value = objective_value;
            stop = clock();
            best_time = (double)(stop - start) / CLOCKS_PER_SEC;
        }
    }
    
    void local_search::verify_solution() {

    }
    
    void local_search::display(std::ostream& out) {

    }

    void local_search::add_clause(unsigned sz, literal const* c) {
        add_cardinality(sz, c, sz - 1);
    }

    void local_search::add_cardinality(unsigned sz, literal const* c, unsigned k) {
        unsigned id = m_num_constraints;
        ++m_num_constraints;
        // constraint_term.push_back(svector<term>());
        for (unsigned i = 0; i < sz; ++i) {
            m_num_vars = std::max(c[i].var() + 1, m_num_vars);
            // var_term.reserve(c[i].var() + 1);
            term t;
            t.constraint_id = id;
            t.var_id = c[i].var();
            t.sense =  c[i].sign();
            var_term[t.var_id].push_back(t);
            constraint_term[id].push_back(t);
        }
        constraint_k.push_back(k);                
    }

    local_search::local_search(solver& s) {

        m_num_vars = 0;
        m_num_constraints = 0;

        // copy units
        unsigned trail_sz = s.init_trail_size();
        for (unsigned i = 0; i < trail_sz; ++i) {
            add_clause(1, s.m_trail.c_ptr() + i);
        }

        // copy binary clauses
        {
            unsigned sz = s.m_watches.size();
            for (unsigned l_idx = 0; l_idx < sz; ++l_idx) {
                literal l = ~to_literal(l_idx);
                watch_list const & wlist = s.m_watches[l_idx];
                watch_list::const_iterator it  = wlist.begin();
                watch_list::const_iterator end = wlist.end();
                for (; it != end; ++it) {
                    if (!it->is_binary_non_learned_clause())
                        continue;
                    literal l2 = it->get_literal();
                    if (l.index() > l2.index()) 
                        continue;
                    literal ls[2] = { l, l2 };
                    add_clause(2, ls);
                }
            }
        }

        // copy clauses
        clause_vector::const_iterator it =  s.m_clauses.begin();
        clause_vector::const_iterator end = s.m_clauses.end();
        for (; it != end; ++it) {
            clause& c = *(*it);
            add_clause(c.size(), c.begin());
        }

        // copy cardinality clauses
        card_extension* ext = dynamic_cast<card_extension*>(s.get_extension());
        if (ext) {
            literal_vector lits;
            unsigned sz = ext->m_cards.size();
            for (unsigned i = 0; i < sz; ++i) {
                card_extension::card& c = *ext->m_cards[i];
                unsigned n = c.size();
                unsigned k = c.k();

                // c.lit() <=> c.lits() >= k
                // 
                //    (c.lits() < k) or c.lit()
                // =  (c.lits() + (n - k - 1)*~c.lit()) <= n    
                //
                //    ~c.lit() or (c.lits() >= k)
                // =  ~c.lit() or (~c.lits() <= n - k)
                // =  k*c.lit() + ~c.lits() <= n 
                // 

                lits.reset();
                for (unsigned j = 0; j < n; ++j) lits.push_back(c[j]);
                for (unsigned j = 0; j < n - k - 1; ++j) lits.push_back(~c.lit());
                add_cardinality(lits.size(), lits.c_ptr(), n);
                
                lits.reset();
                for (unsigned j = 0; j < n; ++j) lits.push_back(~c[j]);
                for (unsigned j = 0; j < k; ++j) lits.push_back(c.lit());
                add_cardinality(lits.size(), lits.c_ptr(), n);
            }
            //
            // optionally handle xor constraints.
            //
            SASSERT(ext->m_xors.empty());            
        }
    }
    
    local_search::~local_search() {
    }
    
    void local_search::add_soft(int v, int weight) {
        // TBD
        ob_term t;
        t.var_id = v;
        t.coefficient = weight;
        ob_constraint.push_back(t);
    }
    
    lbool local_search::operator()() {
        //sat_params params;
        //std::cout << "my parameter value: " << params.cliff() << "\n";
        init();
        bool reach_cutoff_time = false;
        bool reach_known_best_value = false;
        bool_var flipvar;
        double elapsed_time = 0;
        //clock_t start = clock(), stop;  // TBD, use stopwatch facility
        start = clock();
        // ################## start ######################
        //std::cout << "Start initialize and local search, restart in every " << max_steps << " steps\n";
        unsigned tries, step;
        for (tries = 0; ; ++tries) {
            reinit();
            for (step = 1; step <= max_steps; ++step) {
                // feasible
                if (m_unsat_stack.empty()) {
                    calculate_and_update_ob();
                    if (best_objective_value >= best_known_value) {
                        reach_known_best_value = true;
                        break;
                    }
                }
                flipvar = pick_var();
                flip(flipvar);
                time_stamp[flipvar] = step;
            }
            // take a look at watch
            stop = clock();
            elapsed_time = (double)(stop - start) / CLOCKS_PER_SEC;
            if (tries % 10 == 0) 
                std::cout << tries << ": " << elapsed_time << '\n';
            if (elapsed_time > cutoff_time)
                reach_cutoff_time = true;
            //if (reach_known_best_value || reach_cutoff_time)
            //    break;
        }
        if (reach_known_best_value) {
            std::cout << elapsed_time << "\n";
        }
        else {
            std::cout << -1 << "\n";
        }
        //print_solution();
        std::cout << tries * max_steps + step << '\n';
        // TBD: adjust return status
        return l_undef;
    }

    void local_search::flip(bool_var flipvar)
    {
		static unsigned_vector hack_vector;
		hack_vector.reset();
        // already changed truth value!!!!
        cur_solution[flipvar] = !cur_solution[flipvar];

        unsigned v, c;
        int org_flipvar_score = score(flipvar);
        int org_flipvar_sscore = sscore[flipvar];

        // update related clauses and neighbor vars
        svector<term> const& constraints = var_term[flipvar];
        unsigned num_cs = constraints.size();
        for (unsigned i = 0; i < num_cs; ++i) {
            c = constraints[i].constraint_id;
            if (cur_solution[flipvar] == constraints[i].sense) {
                //++true_terms_count[c];
                --constraint_slack[c];
                switch (constraint_slack[c]) {
                case -2:  // from -1 to -2
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        // flipping the slack increasing var will no long sat this constraint
                        if (cur_solution[v] == constraint_term[c][j].sense)
                            //score[v] -= constraint_weight[c];
                            --m_var_info[v].m_score;
                    }
                    break;
                case -1: // from 0 to -1: sat -> unsat
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        ++cscc[v];
                        //score[v] += constraint_weight[c];
                        ++m_var_info[v].m_score;
						hack_vector.push_back(v);
                        // slack increasing var
                        if (cur_solution[v] == constraint_term[c][j].sense)
                            ++sscore[v];
                    }
                    unsat(c);
                    break;
                case 0: // from 1 to 0
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        // flip the slack decreasing var will falsify this constraint
                        if (cur_solution[v] != constraint_term[c][j].sense) {
                            //score[v] -= constraint_weight[c];
                            --m_var_info[v].m_score;
                            --sscore[v];
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            else { // if (cur_solution[flipvar] != var_term[i].sense)
                //--true_terms_count[c];
                ++constraint_slack[c];
                switch (constraint_slack[c]) {
                case 1: // from 0 to 1
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        // flip the slack decreasing var will no long falsify this constraint
                        if (cur_solution[v] != constraint_term[c][j].sense) {
                            //score[v] += constraint_weight[c];
                            ++m_var_info[v].m_score;
							hack_vector.push_back(v);
                            ++sscore[v];
                        }
                    }
                    break;
                case 0: // from -1 to 0: unsat -> sat
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        ++cscc[v];
                        //score[v] -= constraint_weight[c];
                        --m_var_info[v].m_score;
                        // slack increasing var no longer sat this var
                        if (cur_solution[v] == constraint_term[c][j].sense)
                            --sscore[v];
                    }
                    sat(c);
                    break;
                case -1: // from -2 to -1
                    for (unsigned j = 0; j < constraint_term[c].size(); ++j) {
                        v = constraint_term[c][j].var_id;
                        // flip the slack increasing var will satisfy this constraint
						if (cur_solution[v] == constraint_term[c][j].sense) {
							//score[v] += constraint_weight[c];
							++m_var_info[v].m_score;
							hack_vector.push_back(v);
						}
                    }
                    break;
                default:
                    break;
                }
            }
        }

        m_var_info[flipvar].m_score = -org_flipvar_score;
        sscore[flipvar] = -org_flipvar_sscore;

        m_var_info[flipvar].m_conf_change = false;
        cscc[flipvar] = 0;

        /* update CCD */
        // remove the vars no longer goodvar in goodvar stack
        for (unsigned i = goodvar_stack.size(); i > 0;) {
            --i;
            v = goodvar_stack[i];
            if (score(v) <= 0) {
                goodvar_stack[i] = goodvar_stack.back();
                goodvar_stack.pop_back();
                m_var_info[v].m_in_goodvar_stack = false;
            }
        }
        // update all flipvar's neighbor's conf_change to true, add goodvar/okvar

#if 0
        unsigned sz = var_neighbor[flipvar].size();
        for (unsigned i = 0; i < sz; ++i) {
            v = var_neighbor[flipvar][i];
            m_var_info[v].m_conf_change = true;
            if (score(v) > 0 && !already_in_goodvar_stack(v)) {
                goodvar_stack.push_back(v);
                m_var_info[v].m_in_goodvar_stack = true;
            }
        }
#else
		unsigned sz = hack_vector.size();
		for (unsigned i = 0; i < sz; ++i)
		{
			v = hack_vector[i];
			m_var_info[v].m_conf_change = true;
			if (score(v) > 0 && !already_in_goodvar_stack(v)) {
				goodvar_stack.push_back(v);
				m_var_info[v].m_in_goodvar_stack = true;
			}
		}
#endif
    }

    bool local_search::tie_breaker_sat(bool_var v, bool_var best_var)  {
        // most improvement on objective value
        int v_imp = cur_solution[v] ? -coefficient_in_ob_constraint.get(v, 0) : coefficient_in_ob_constraint.get(v, 0);
        int b_imp = cur_solution[best_var] ? -coefficient_in_ob_constraint.get(best_var, 0) : coefficient_in_ob_constraint.get(best_var, 0);
        //std::cout << v_imp << "\n";
        // break tie 1: max imp
        // break tie 2: conf_change
        // break tie 3: time_stamp
        
        return 
            (v_imp > b_imp) ||
            ((v_imp == b_imp) && 
             ((conf_change(v) && !conf_change(best_var)) ||
              ((conf_change(v) == conf_change(best_var)) && 
               (time_stamp[v] < time_stamp[best_var]))));
    }

    bool local_search::tie_breaker_ccd(bool_var v, bool_var best_var)  {
        // break tie 1: max score
        // break tie 2: max sscore
        // break tie 3: cscc
        // break tie 4: oldest one
        return 
            ((score(v) > score(best_var)) ||
             ((score(v) == score(best_var)) &&
              ((sscore[v] > sscore[best_var]) ||
               ((sscore[v] == sscore[best_var]) &&
                ((cscc[v] > cscc[best_var]) ||
                 ((cscc[v] == cscc[best_var]) && 
                  (time_stamp[v] < time_stamp[best_var])))))));
    }

    bool_var local_search::pick_var() {
        int c, v;
        bool_var best_var = 0;

        // SAT Mode
        if (m_unsat_stack.empty()) {
            //++as;
            for (unsigned i = 0; i < ob_constraint.size(); ++i) {
                v = ob_constraint[i].var_id;
                if (tie_breaker_sat(v, best_var))
                    best_var = v;
            }
            return best_var;
        }

        // Unsat Mode: CCD > RD
        // CCD mode
        if (!goodvar_stack.empty()) {
            //++ccd;
            best_var = goodvar_stack[0];
            for (unsigned i = 1; i < goodvar_stack.size(); ++i) {
                v = goodvar_stack[i];
                if (tie_breaker_ccd(v, best_var))
                    best_var = v;
            }
            return best_var;
        }

        // Diversification Mode
        c = m_unsat_stack[rand() % m_unsat_stack.size()]; // a random unsat constraint
        // Within c, from all slack increasing var, choose the oldest one
        unsigned c_size = constraint_term[c].size();
        for (unsigned i = 0; i < c_size; ++i) {
            v = constraint_term[c][i].var_id;
            if (cur_solution[v] == constraint_term[c][i].sense && time_stamp[v] < time_stamp[best_var])
                best_var = v;
        }
        return best_var;
    }

    void local_search::set_parameters()  {

        srand(m_config.seed());
        cutoff_time = m_config.cutoff_time();
        s_id = m_config.strategy_id();
        best_known_value = m_config.best_known_value();



        if (s_id == 0)
            max_steps = 2 * num_vars();
        else {
            std::cout << "Invalid strategy id!" << std::endl;
            exit(-1);
        }

        /*std::cout << "seed:\t" << m_config.seed() << '\n';
          std::cout << "cutoff time:\t" << m_config.cutoff_time() << '\n';
          std::cout << "strategy id:\t" << m_config.strategy_id() << '\n';
          std::cout << "best_known_value:\t" << m_config.best_known_value() << '\n';
          std::cout << "max_steps:\t" << max_steps << '\n';
        */
    }

    void local_search::print_info() {
        for (int v = 1; v <= num_vars(); ++v) {
            std::cout << var_neighbor[v].size() << '\t' << cur_solution[v] << '\t' << conf_change(v) << '\t' << score(v) << '\t' << sscore[v] << '\n';
        }
    }
}
