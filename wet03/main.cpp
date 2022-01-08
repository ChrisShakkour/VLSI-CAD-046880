#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include <iostream>
#include <string>

#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
#include "core/Solver.h"

using namespace Minisat;
using namespace std;

bool verbose = false;

///////////////////////////////////////////////////////////////////////////

/* functions declarations */
void Instance_Add_Clauses(hcmInstance* inst,Solver &S,vector<hcmInstance*> &vec_dff);
bool compatible_cells(hcmCell *flatCell_spec,hcmCell *flatCell_imp, 
  std::map< hcmNode*, hcmNode* > &outputs_cells);
void add_dffs_to_map(hcmInstance* spec_inst,hcmInstance* imp_inst,std::map< hcmNode*, hcmNode* > &outputs_cells);
void XOR_outputs(std::map< hcmNode*, hcmNode* > &outputs_cells,Solver &S);

/* implementation in the end  */

int main(int argc, char **argv) {
  int argIdx = 1;
  int anyErr = 0;

  vector<string> specFiles;
  vector<string> implementFiles;
  
  if (argc < 7) {
    anyErr++;
  } 
  else {
    if (!strcmp(argv[argIdx], "-v")) {
          argIdx++;
          verbose = true;
    }
    if (!strcmp(argv[argIdx], "-s")) {
      argIdx++;
      // spec files
      for (;argIdx < argc; argIdx++) {
        if(!strcmp(argv[argIdx], "-i")){
          argIdx++;
          break;
        }
        specFiles.push_back(string(argv[argIdx]));
      }
      if(argIdx==argc){
        cerr << "-E- missing `-i` in the command line  " << endl;
        anyErr++;
      }
      if (specFiles.size() < 2) {
        cerr << "-E- At least spec-cell and single verilog file required for spec model" << endl;
        anyErr++;
      }
      // implementation files
      for (;argIdx < argc; argIdx++) {
        implementFiles.push_back(string(argv[argIdx]));
      }
      if (implementFiles.size() < 2) {
        cerr << "-E- At least implementation-cell and single verilog file required for implementaion model" << endl;
        anyErr++;
      }
    }
    else{
      cerr << "-E- missing `-s` in the command line" << endl;
      anyErr++;
    }
    
  }
  if (anyErr) {
    cerr << "Usage: " << argv[0] << "  [-v] -s spec-cell verilog1 [verilog2...] -i implemenattion-cell verilog1 [verilog2...] \n" ;
    exit(1);
  }
  

  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");
  

  // SPEC design
  hcmDesign* design_spec = new hcmDesign("design_spec");
  string cellName_spec = specFiles[0];
  for (unsigned int i = 1; i < specFiles.size(); i++) {
    printf("-I- (SPEC) Parsing verilog %s ...\n", specFiles[i].c_str());
    // cout<<  <<endl;
    if (!design_spec->parseStructuralVerilog(specFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << specFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  hcmCell *topCell_spec = design_spec->getCell(cellName_spec);
  if (!topCell_spec) {
    printf("-E- could not find cell %s\n", cellName_spec.c_str());
    exit(1);
  }

  // IMPLEMENTATION design
  hcmDesign* design_imp = new hcmDesign("design_imp");
  string cellName_imp = implementFiles[0];
  for (unsigned int i = 1; i < implementFiles.size(); i++) {
    printf("-I- (IMPLEMENTATION) Parsing verilog %s ...\n", implementFiles[i].c_str());
    if (!design_imp->parseStructuralVerilog(implementFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << implementFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  hcmCell *topCell_imp = design_imp->getCell(cellName_imp);
  if (!topCell_imp) {
    printf("-E- could not find cell %s\n", cellName_imp.c_str());
    exit(1);
  }

  // Flattening the topcells
  hcmCell *flatCell_spec = hcmFlatten(cellName_spec + string("_flat"), topCell_spec, globalNodes);
  cout << "-I- Spec-Cell flattened" << endl;
  hcmCell *flatCell_imp = hcmFlatten(cellName_imp + string("_flat"), topCell_imp, globalNodes);
  cout << "-I- implementaion-Cell flattened" << endl;

  // map to save pairs of outputs nodes of each cell (those which are needed to be compared)
  std::map< hcmNode*, hcmNode* > outputs_cells; // first - spec node , second - implementation node.

  // check if cells are compatible for FEV, and if so push all output ports to the above map
  bool compatible = compatible_cells(flatCell_spec,flatCell_imp,outputs_cells);
  if(!compatible){
    cerr<<"-E Cells aren't compatible for FEV , aborting" <<endl;
    exit(1);
  }


  // introduce a number for each node (0,1,2,3...) 
  std::map< std::string, hcmNode* >::const_iterator nI;
  std::map< std::string, hcmNode* > spec_nodes = flatCell_spec->getNodes();
  int var_num=0, num_nodes=spec_nodes.size();
  int vdd_num = num_nodes-1, vss_num= num_nodes-2;

  for (nI =spec_nodes.begin(); nI != spec_nodes.end(); nI++){
    hcmNode *node= nI->second;   
    if(node->getName()=="VDD"){
      node->setProp("variable_num",vdd_num);  
    }
    else if(node->getName()=="VSS"){ 
      node->setProp("variable_num",vss_num); 
    }
    else{
      node->setProp("variable_num",var_num); 
      var_num++;
    }
  }
  
  // At the IMPLEMENTATION cell, match common nodes from the SPEC cell
  // and introduce new numbers for non-input nodes.
  // note that for input node which we've already created a variable
  // in the SPEC cell we give the same variable numeber
  var_num=num_nodes;
  std::map< std::string, hcmNode* > imp_nodes = flatCell_imp->getNodes();
  for (nI =imp_nodes.begin(); nI != imp_nodes.end(); nI++){
    hcmNode *node= nI->second;
    string node_name=node->getName();
    if(node_name=="VSS" || node_name=="VDD"){
      int glob = vss_num+(node_name=="VDD");
      node->setProp("variable_num",glob);
      continue;
    }
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      hcmNode *node_from_spec = flatCell_spec->getNode(node_name);
      if(node_from_spec){  // a variable with the same name exist in the spec_cell
        int temp=0;
        node_from_spec->getProp("variable_num",temp);
        node->setProp("variable_num",temp);
      }
      else{
        node->setProp("variable_num",var_num); 
        var_num++;
      } 
    }
    else{
      node->setProp("variable_num",var_num); 
      var_num++;
    }
   
  } 

  cout<<"\nVariable mapping for each cell : \n"<<endl;
  std::map<int, string> input_var_to_name;
  for (nI =spec_nodes.begin(); nI != spec_nodes.end(); nI++){
    hcmNode *node= nI->second;
    int temp=0;
    node->getProp("variable_num",temp); 
    string name = node->getName();
    cout<<name<< " = " <<temp <<endl;
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      input_var_to_name.insert(std::pair<int, string>(temp,name));
    }
  } 
  cout << " ---- " <<endl;
  for (nI =imp_nodes.begin(); nI != imp_nodes.end(); nI++){
    hcmNode *node= nI->second;
    int temp=0;
    node->getProp("variable_num",temp); 
    string name = node->getName();
    cout<<name<< " = " <<temp <<endl;
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      input_var_to_name.insert(std::pair<int, string>(temp,name));
    } 
  } 

  cout <<" "<<endl;
  Solver S;
  // Declare all the variables (which is currently var_num-1 vars)
  for(int i=0;i<var_num;i++){
    S.newVar();
  }
  // Forcing VDD=1 , VSS=0
  S.addClause(~mkLit(vss_num));
  S.addClause(mkLit(vdd_num));
 
  //vectors for saving DFF of the cells (later they are considered as outputs which are need to be compared)
  vector<hcmInstance*> vec_dff_spec;
  vector<hcmInstance*> vec_dff_imp;

  // creating appropriate tsyitin clauses to each instance in each of the cells
  std::map< std::string, hcmInstance* >::const_iterator iI;
  std::map< std::string, hcmInstance* > instances_spec = flatCell_spec->getInstances();
  for(iI =instances_spec.begin(); iI != instances_spec.end(); iI++){
    hcmInstance* inst= iI->second;
    Instance_Add_Clauses(inst,S,vec_dff_spec);
  }
  std::map< std::string, hcmInstance* > instances_imp = flatCell_imp->getInstances();
  for(iI =instances_imp.begin(); iI != instances_imp.end(); iI++){
    hcmInstance* inst= iI->second;
    Instance_Add_Clauses(inst,S,vec_dff_imp);
  }

  //Adding inputs nodes of the same DFF to the outputs map ()
  std::vector<hcmInstance*>::const_iterator it_spec;
  for(it_spec=vec_dff_spec.begin(); it_spec!=vec_dff_spec.end(); it_spec++){
    hcmInstance* spec_inst = *it_spec;
    string spec_name = spec_inst->getName();
    std::vector<hcmInstance*>::const_iterator it_imp;
    for(it_imp=vec_dff_imp.begin(); it_imp!=vec_dff_imp.end(); it_imp++){
      hcmInstance* imp_inst = *it_spec;
      if(spec_name==imp_inst->getName()){
        add_dffs_to_map(spec_inst,imp_inst,outputs_cells);
      }
    }
    
  }

  // std::map< hcmNode*, hcmNode* >::const_iterator I;
  // for(I =outputs_cells.begin(); I != outputs_cells.end(); I++){
  //   hcmNode* o1 = I->first;
  //   hcmNode* o2 = I->second;
  //   cout<< o1->getName() <<endl;
  //   cout<< o2->getName() <<endl;
  // }

  // Adding appropriate tsyitin clauses to each output (including DFF inputs)
  // in other words: xor clause between appropriate outputs (DFF inputs as well)
  XOR_outputs(outputs_cells,S);
  
  // solve with minisat
  S.simplify();
  bool sat = S.solve();
  if(sat){
    cout << "SATISFIABLE" <<endl;
    cout << "Input assignment:" <<endl;
    // for(int i=0; i<var_num;i++){
    //   printf("%d = %s\n",i, (S.model[i]== l_Undef) ? "undef" : ((S.model[i]== l_True) ? "+" : "-"));
    // }
    std::map< int, string >::const_iterator inpuI;
    for(inpuI=input_var_to_name.begin(); inpuI!=input_var_to_name.end();inpuI++){
      string input_name = inpuI->second;
      int i = inpuI->first;
      cout << input_name;
      printf(" = %s\n", (S.model[i]== l_Undef) ? "undef" : ((S.model[i]== l_True) ? "+" : "-"));
    }
  } 
  else{
    cout << "UNSAT" <<endl;
  }
  return(0);
}

// Function's implementations

/*
  Add xor clauses between outputs of cells, add introduce new variable as output
  of the xor. 
  In addition we add  more clause with OR of all the XOR outputs (as if one of them is 1 the problem is SAT).
  And one more clause to force the result of OR to be 1 (as shown in class).
*/
void XOR_outputs(std::map< hcmNode*, hcmNode* > &outputs_cells,Solver &S){
  std::map< hcmNode*, hcmNode* >::const_iterator I;
  vector<int> XOR_results;
  for(I =outputs_cells.begin(); I != outputs_cells.end(); I++){
    hcmNode* spec_node = I->first;
    hcmNode* imp_node = I->second;
    int v = S.newVar();
    XOR_results.push_back(v);
    int a,b;
    spec_node->getProp("variable_num",a);
    imp_node->getProp("variable_num",b);

    // v = a xor b   clause
    S.addClause(~mkLit(a),~mkLit(b),~mkLit(v)); // (~A+ ~B+ ~V)
    S.addClause(mkLit(a),mkLit(b),~mkLit(v)); // (A+ B+ ~V)
    S.addClause(mkLit(a),~mkLit(b),mkLit(v)); // (A+ ~B+ V)
    S.addClause(~mkLit(a),mkLit(b),mkLit(v)); // (~A+ B+ V)

  }

  int OR_result = S.newVar();
  // clause of OR between all XOR's outputs
  vec<Lit> clauseLiterals;
  clauseLiterals.push(~mkLit(OR_result));
  
  std::vector<int>::const_iterator it;
  for(it=XOR_results.begin(); it!=XOR_results.end(); it++){
    int var = *it;
    clauseLiterals.push(mkLit(var));
    S.addClause(mkLit(OR_result),~mkLit(var));
  }
  S.addClause(clauseLiterals);
  clauseLiterals.clear();

  // forcing OR result to be 1
  S.addClause(mkLit(OR_result));
}


/*
 in DFF we consider the inputs as outputs which need to be compared :
 we add pairs of the same input (by name of the port) to the output map.
*/
void add_dffs_to_map(hcmInstance* spec_inst,hcmInstance* imp_inst,std::map< hcmNode*, hcmNode* > &outputs_cells){
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  std::map<std::string, hcmInstPort* >spec_inst_ports =spec_inst->getInstPorts();
  for (ipI =spec_inst_ports.begin(); ipI != spec_inst_ports.end(); ipI++){
    hcmInstPort* ip_spec= ipI->second;
    if(ip_spec->getPort()->getDirection()==IN){
      hcmNode* node_spec= ip_spec->getNode();

      std::map<std::string, hcmInstPort* >imp_inst_ports =imp_inst->getInstPorts();
      std::map<std::string, hcmInstPort* >::const_iterator I;
      for (I =imp_inst_ports.begin(); I != imp_inst_ports.end(); I++){
        hcmInstPort* ip_imp= I->second;
        if(ip_imp->getPort()->getName()==ip_spec->getPort()->getName()){
          hcmNode* node_imp= ip_imp->getNode();
          outputs_cells.insert(std::pair<hcmNode*, hcmNode*>(node_spec,node_imp));
        }
      }
    }
  }

}

void logic_Inverter(hcmInstance* inst,Solver &S,bool inverted){
  int input_var, output_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      node->getProp("variable_num",input_var);
    }
  }
  // cout << output_var <<endl;
  // cout << input_var <<endl;
  if(!inverted){
    S.addClause(mkLit(output_var),mkLit(input_var));
    S.addClause(~mkLit(output_var),~mkLit(input_var));    
  }
  else{
    S.addClause(~mkLit(output_var),mkLit(input_var));
    S.addClause(mkLit(output_var),~mkLit(input_var));
  }

}


void logic_AND(hcmInstance* inst,Solver &S, bool inverted){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);
    }
  }
  // cout << output_var <<endl;
  //Building Tseytin clauses
  vec<Lit> clauseLiterals;
  if(!inverted) clauseLiterals.push(mkLit(output_var));
  else clauseLiterals.push(~mkLit(output_var));
  
  std::vector<int>::const_iterator it;
  for(it=input_var.begin(); it!=input_var.end(); it++){
    int var = *it;
    // cout << var <<endl;
    clauseLiterals.push(~mkLit(var));
    if(!inverted) S.addClause(~mkLit(output_var),mkLit(var));
    else S.addClause(mkLit(output_var),mkLit(var));
  }
  S.addClause(clauseLiterals);
  clauseLiterals.clear();
}

void logic_OR(hcmInstance* inst,Solver &S, bool inverted){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);

    }
  }
  // cout << output_var <<endl;
  //Building Tseytin clauses
  vec<Lit> clauseLiterals;
  if(!inverted) clauseLiterals.push(~mkLit(output_var));
  else clauseLiterals.push(mkLit(output_var));
  
  std::vector<int>::const_iterator it;
  for(it=input_var.begin(); it!=input_var.end(); it++){
    int var = *it;
    // cout << var <<endl;
    clauseLiterals.push(mkLit(var));
    if(!inverted) S.addClause(mkLit(output_var),~mkLit(var));
    else S.addClause(~mkLit(output_var),~mkLit(var));
  }
  S.addClause(clauseLiterals);
  clauseLiterals.clear();
}

void logic_XOR(hcmInstance* inst,Solver &S, bool inverted){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);
    }
  }
  //Building Tseytin clauses
  if(!inverted){
    S.addClause(~mkLit(input_var[0]),~mkLit(input_var[1]),~mkLit(output_var)); // (~A+ ~B+ ~C)
    S.addClause(mkLit(input_var[0]),mkLit(input_var[1]),~mkLit(output_var)); // (A+ B+ ~C)
    S.addClause(mkLit(input_var[0]),~mkLit(input_var[1]),mkLit(output_var)); // (A+ ~B+ C)
    S.addClause(~mkLit(input_var[0]),mkLit(input_var[1]),mkLit(output_var)); // (~A+ B+ C)
  }
  else{
    S.addClause(~mkLit(input_var[0]),~mkLit(input_var[1]),mkLit(output_var)); // (~A+ ~B+ C)
    S.addClause(mkLit(input_var[0]),mkLit(input_var[1]),mkLit(output_var)); // (A+ B+ C)
    S.addClause(mkLit(input_var[0]),~mkLit(input_var[1]),~mkLit(output_var)); // (A+ ~B+ ~C)
    S.addClause(~mkLit(input_var[0]),mkLit(input_var[1]),~mkLit(output_var)); // (~A+ B+ ~C)
  }
}

void Instance_Add_Clauses(hcmInstance* inst,Solver &S,vector<hcmInstance*> &vec_dff){
  string logic_name= inst->masterCell()->getName();
  if(logic_name.find("nor")!=std::string::npos){
    logic_OR(inst,S,true);
    // cout<< "$nor" <<endl;
  } else if(logic_name.find("xnor")!=std::string::npos){
    logic_XOR(inst,S,true);
    // cout<< "$xnor" <<endl;
  } else if(logic_name.find("xor")!=std::string::npos){
    logic_XOR(inst,S,false);
    // cout<< "$xor" <<endl;
  } else if(logic_name.find("or")!=std::string::npos){
    logic_OR(inst,S,false);
    // cout<< "$or" <<endl;
  } else if(logic_name.find("nand")!=std::string::npos){
    logic_AND(inst,S,true);
    // cout<< "$nand" <<endl;
  } else if(logic_name.find("and")!=std::string::npos){
    logic_AND(inst,S,false);
    // cout<< "$and" <<endl;
  } else if(logic_name.find("buffer")!=std::string::npos){
    logic_Inverter(inst,S,true); //buffer is an inverted inverter :)
    // cout<< "$buffer" <<endl;
  } else if(logic_name.find("inv")!=std::string::npos){
    logic_Inverter(inst,S,false);
    // cout<< "$inv" <<endl;
  } else if(logic_name.find("not")!=std::string::npos){
    logic_Inverter(inst,S,false);
    // cout<< "$inv" <<endl;
  } else if(logic_name.find("dff")!=std::string::npos){
    // remember DFF inputs (used as "outputs") are needed to be compared between the cells 
    // so we dave them in the dff_vector for later
    vec_dff.push_back(inst);
    // cout<< "$dff" <<endl;
  } else{
    cerr << "-E- does not support gate type: " << logic_name << " aborting." << endl;
    exit(1);
  }
}


/*
  This function return true iff the spec cell and implementation cell
  have the exact amount of output ports, and each have same output names,
  and the same DFF amount and with same names (as their inputs will be considered as outputs).
  If one of the above doesn't happen, we return false as the cells are incompatible for FEV.
*/

bool compatible_cells(hcmCell *flatCell_spec,hcmCell *flatCell_imp, 
  std::map< hcmNode*, hcmNode* > &outputs_cells){
  //check if every output name match between the 2 cells
  int out_ports_spec =0 ,out_ports_imp=0 ;
  vector<hcmPort*> spec_ports = flatCell_spec->getPorts();
  std::vector<hcmPort*>::const_iterator pI;
  for(pI=spec_ports.begin(); pI!=spec_ports.end(); pI++){
    hcmPort* port_sp = *pI;
    if(port_sp->getDirection() == OUT){
      out_ports_spec++;
      string port_name =port_sp->getName();
      hcmPort * port_imp = flatCell_imp->getPort(port_name);
      if(!port_imp){
        cerr<<"-E Different names of output ports between the cells" <<endl;
        return false;
      }
      outputs_cells.insert(std::pair<hcmNode*, hcmNode*>(port_sp->owner(),port_imp->owner()));
    }
  }
  // check if both cells have the same amount of outputs ports
  vector<hcmPort*> imp_ports = flatCell_imp->getPorts();
  for(pI=imp_ports.begin(); pI!=imp_ports.end(); pI++){
    hcmPort* port_imp = *pI;
    if(port_imp->getDirection() == OUT){
      out_ports_imp++;
    }
  }
  if(out_ports_imp!=out_ports_spec) {
    cerr<<"-E Different amount of output ports between the cells" <<endl;
    return false;
  }

  //check if every DFF name match between the 2 cells
  int DFF_spec =0 ,DFF_imp=0 ;
  std::map< std::string, hcmInstance* >::const_iterator iI;
  std::map< std::string, hcmInstance* > instances_spec = flatCell_spec->getInstances();
  for(iI =instances_spec.begin(); iI != instances_spec.end(); iI++){
    hcmInstance* inst_spec= iI->second;
    string logic_name= inst_spec->masterCell()->getName();
    if(logic_name.find("dff")!=std::string::npos){
      DFF_spec++;
      string inst_name= inst_spec->getName();
      hcmInstance *inst_imp =flatCell_imp->getInst(inst_name);
      if(!inst_imp){
        cerr<<"-E Different names of DFF between the cells" <<endl;
        return false;
      }
    }
  }
  // check if both cells have the same amount of DFF
  std::map< std::string, hcmInstance* > instances_imp = flatCell_imp->getInstances();
  for(iI =instances_imp.begin(); iI != instances_imp.end(); iI++){
    hcmInstance* inst_imp= iI->second;
    string logic_name= inst_imp->masterCell()->getName();
    if(logic_name.find("dff")!=std::string::npos){
      DFF_imp++;
    }
  }
  if(DFF_imp!=DFF_spec) {
    cerr<<"-E Different amount of DFF between the cells" <<endl;
    return false;
  }
  // If haven't returned false yet, it means the cells are compatible for FEV
  return true;
}