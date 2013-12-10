#include "paxos.h"
#include "handle.h"
// #include <signal.h>
#include <stdio.h>

// This module implements the proposer and acceptor of the Paxos
// distributed algorithm as described by Lamport's "Paxos Made
// Simple".  To kick off an instance of Paxos, the caller supplies a
// list of nodes, a proposed value, and invokes the proposer.  If the
// majority of the nodes agree on the proposed value after running
// this instance of Paxos, the acceptor invokes the upcall
// paxos_commit to inform higher layers of the agreed value for this
// instance.


bool
operator> (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m > b.m));
}

bool
operator>= (const prop_t &a, const prop_t &b)
{
  return (a.n > b.n || (a.n == b.n && a.m >= b.m));
}

std::string
print_members(const std::vector<std::string> &nodes)
{
  std::string s;
  s.clear();
  for (unsigned i = 0; i < nodes.size(); i++) {
    s += nodes[i];
    if (i < (nodes.size()-1))
      s += ",";
  }
  return s;
}

bool isamember(std::string m, const std::vector<std::string> &nodes)
{
  for (unsigned i = 0; i < nodes.size(); i++) {
    if (nodes[i] == m) return 1;
  }
  return 0;
}

bool
proposer::isrunning()
{
  bool r;
  assert(pthread_mutex_lock(&pxs_mutex)==0);
  r = !stable;
  assert(pthread_mutex_unlock(&pxs_mutex)==0);
  return r;
}

// check if the servers in l2 contains a majority of servers in l1
bool
proposer::majority(const std::vector<std::string> &l1, 
		const std::vector<std::string> &l2)
{
  unsigned n = 0;

  for (unsigned i = 0; i < l1.size(); i++) {
    if (isamember(l1[i], l2))
      n++;
  }
  return n >= (l1.size() >> 1) + 1;
}

proposer::proposer(class paxos_change *_cfg, class acceptor *_acceptor, 
		   std::string _me)
  : cfg(_cfg), acc (_acceptor), me (_me), break1 (false), break2 (false), 
    stable (true)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);
}

void
proposer::setn()
{
  my_n.n = acc->get_n_h().n + 1 > my_n.n + 1 ? acc->get_n_h().n + 1 : my_n.n + 1;
  // also update my_n.m
  my_n.m = me;
}

//initiate Paxos proposer to have "members" agree to value="newv" 
//for the instance numbered "instance"
bool
proposer::run(int instance, std::vector<std::string> newnodes, std::string newv)
{
  std::vector<std::string> accepts;
  std::vector<std::string> nodes;
  std::vector<std::string> nodes1;
  std::string v;
  bool r = false;

  pthread_mutex_lock(&pxs_mutex);
  printf("start: initiate paxos for %s w. i=%d v=%s stable=%d\n",
	 print_members(newnodes).c_str(), instance, newv.c_str(), stable);
  if (!stable) {  // already running proposer?
    printf("proposer::run: already running\n");
    pthread_mutex_unlock(&pxs_mutex);
    return false;
  }

  setn();
  accepts.clear();
  nodes.clear();
  v.clear();
  // update stable, c_nodes and c_v for this run
  nodes = c_nodes = newnodes;
  c_v = newv;
  stable = false;

  if (prepare(instance, accepts, nodes, v)) {

    if (majority(c_nodes, accepts)) {
      printf("paxos::manager: received a majority of prepare responses\n");

      if (v.size() == 0) {
        v = c_v;
      }

      breakpoint1();

      nodes1 = accepts;
      accepts.clear();
      accept(instance, accepts, nodes1, v);

      if (majority(c_nodes, accepts)) {
	printf("paxos::manager: received a majority of accept responses\n");

	breakpoint2();

	decide(instance, accepts, v);
	r = true;
      } else {
	printf("paxos::manager: no majority of accept responses\n");
      }
    } else {
      printf("paxos::manager: no majority of prepare responses\n");
    }
  } else {
    printf("paxos::manager: prepare is rejected %d\n", stable);
  }
  stable = true;
  pthread_mutex_unlock(&pxs_mutex);
  return r;
}

bool
proposer::prepare(unsigned instance, std::vector<std::string> &accepts, 
         std::vector<std::string> nodes,
         std::string &v)
{
  printf("proposer::prepare: calling prepare for instance %u\n", instance);
  // keep track of the highest n_a and its v_a, first set it to min
  prop_t max_n_a;
  max_n_a.n = 0;
  max_n_a.m = "";

  // init preparearg
  paxos_protocol::preparearg arg;
  arg.instance = instance;
  arg.n = my_n;
  arg.v = "";

  for (int i = 0; i < nodes.size(); i++) {
    handle hd(nodes[i]);
    rpcc *cl = hd.get_rpcc();
    // fail to connect
    if (!cl) {
      printf("proposer::prepare: cannot connect to %s\n", nodes[i].c_str());
      continue;
    }
    // call prepare RPC to each node
    printf("proposer::prepare: calling preparereq RPC to %s\n", nodes[i].c_str());
    paxos_protocol::prepareres res;
    res.oldinstance = 0;
    res.accept = 0;

    if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("proposer::prepare: cannot unlock pxs_mutex\n");
      return false;
    }
    paxos_protocol::status ret = cl->call(paxos_protocol::preparereq, me, arg, res, rpcc::to(1000));
    if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("proposer::prepare: cannot lock pxs_mutex\n");
      return false;
    }

    if (paxos_protocol::OK != ret) {
      printf("proposer::prepare: got ERR\n");
      continue;
    }
    printf("proposer::prepare: got OK\n");
    // the node accpeted the prepare
    if (1 == res.accept) {
      printf("proposer::prepare: %s accpeted the preparereq RPC\n", nodes[i].c_str());
      // add it to the accepts list
      accepts.push_back(nodes[i]);
      // keep track of the highest n_a and its v_a
      if (res.n_a > max_n_a) {
        printf("proposer::prepare: update max_n_a\n");
        max_n_a = res.n_a;
        v = res.v_a;
      }
    }
    else {
      printf("proposer::prepare: not accept\n");
      // res.accept == 0 && res.oldinstance == 0
      if (0 == res.oldinstance) {
        printf("proposer::prepare: %s rejected the preparereq RPC\n", nodes[i].c_str());
        continue;
      }
      // the node said the instance is old
      printf("proposer::prepare: %s replied oldinstance for preparereq RPC\n", nodes[i].c_str());
      // update value
      acc->commit(instance, res.v_a);  
    }
  }
  return true;
}


void
proposer::accept(unsigned instance, std::vector<std::string> &accepts,
        std::vector<std::string> nodes, std::string v)
{
  printf("proposer::accept: calling accept for instance %u\n", instance);
  // init acceptarg
  paxos_protocol::acceptarg arg;
  arg.instance = instance;
  arg.n = my_n;
  arg.v = v;

  for (int i = 0; i < nodes.size(); ++i) {
    handle hd(nodes[i]);
    rpcc *cl = hd.get_rpcc();
    // fail to connect
    if (!cl) {
      printf("proposer::accept: cannot connect to %s\n", nodes[i].c_str());
      continue;
    }
    // call accept RPC to each node
    printf("proposer::accept: calling acceptreq RPC to %s\n", nodes[i].c_str());
    int r = 0;
    if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("proposer::accept: cannot unlock pxs_mutex\n");
      return;
    }
    paxos_protocol::status ret = cl->call(paxos_protocol::acceptreq, me, arg, r, rpcc::to(1000));
    if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("proposer::accept: cannot lock pxs_mutex\n");
      return;
    }

    if (paxos_protocol::OK != ret) {
      printf("proposer::accept: got ERR\n");
      continue;
    }
    printf("proposer::accept: got OK\n");
    // accept is rejected
    if (0 == r) {
      printf("proposer::accept: %s rejected the acceptreq RPC\n", nodes[i].c_str());
      continue;
    }
    // the node accepted the accept
    printf("proposer::accept: %s accpeted the acceptreq RPC\n", nodes[i].c_str());
    // add it to the accepts list
    accepts.push_back(nodes[i]);
  }
}

void
proposer::decide(unsigned instance, std::vector<std::string> accepts, 
	      std::string v)
{
  printf("proposer::decide: calling decide for instance %u\n", instance);
  // init decidearg
  paxos_protocol::decidearg arg;
  arg.instance = instance;
  arg.v = v;

  for (int i = 0; i < accepts.size(); i++) {
    handle hd(accepts[i]);
    rpcc *cl = hd.get_rpcc();
    // fail to connect
    if (!cl) {
      printf("proposer::decide: cannot connect to %s\n", accepts[i].c_str());
      continue;
    }
    // call decide RPC to each node
    printf("proposer::decide: calling decidereq RPC to %s\n", accepts[i].c_str());
    int r = 0;
    if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("proposer::decide: cannot unlock pxs_mutex\n");
      return;
    }
    paxos_protocol::status ret = cl->call(paxos_protocol::decidereq, me, arg, r, rpcc::to(1000));
    if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("proposer::decide: cannot lock pxs_mutex\n");
      return;
    }

    if (paxos_protocol::OK != ret) {
      printf("proposer::decide: got ERR\n");
      continue;
    }
    printf("proposer::decide: got OK\n");
    if (0 == r) {
      // rejected
      printf("proposer::decide: %s rejected the decidereq RPC\n", accepts[i].c_str());
      continue;
    }
    // accepted
    printf("proposer::decide: %s accpeted the decidereq RPC\n", accepts[i].c_str());
  }
}

acceptor::acceptor(class paxos_change *_cfg, bool _first, std::string _me, 
	     std::string _value)
  : cfg(_cfg), me (_me), instance_h(0)
{
  assert (pthread_mutex_init(&pxs_mutex, NULL) == 0);

  n_h.n = 0;
  n_h.m = me;
  n_a.n = 0;
  n_a.m = me;
  v_a.clear();

  l = new log (this, me);

  if (instance_h == 0 && _first) {
    values[1] = _value;
    l->loginstance(1, _value);
    instance_h = 1;
  }

  pxs = new rpcs(atoi(_me.c_str()));
  pxs->reg(paxos_protocol::preparereq, this, &acceptor::preparereq);
  pxs->reg(paxos_protocol::acceptreq, this, &acceptor::acceptreq);
  pxs->reg(paxos_protocol::decidereq, this, &acceptor::decidereq);
}

paxos_protocol::status
acceptor::preparereq(std::string src, paxos_protocol::preparearg a,
    paxos_protocol::prepareres &r)
{
  printf("acceptor::preparereq: received prepare from %s\n", src.c_str());

  // handle a preparereq message from proposer
  if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("acceptor::preparereq: cannot lock pxs_mutex\n");
      return paxos_protocol::ERR;
  }
  if (a.instance <= instance_h) {
    // the instance is old
    printf("acceptor::preparereq: instance %u is old\n", a.instance);
    r.oldinstance = 1;
    r.accept = 0;
    // look up v from existing values
    r.v_a = values[a.instance];
  } else if (a.n > n_h) {
    // accept the prepare
    printf("acceptor::preparereq: accept the prepare\n");
    r.oldinstance = 0;
    r.accept = 1;
    r.n_a = n_a;
    r.v_a = v_a;
    n_h = a.n;
    printf("acceptor::preparereq: n_h is updated, wrtite to the log\n");
    printf("acceptor::preparereq: n_h.n -> %u, n_h.m -> %s\n", n_h.n, n_h.m.c_str());
    l->loghigh(n_h);
  } else {
    // reject the prepare
    printf("acceptor::preparereq: reject the prepare\n");
    r.oldinstance = 0;
    r.accept = 0;
  }
  if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("acceptor::preparereq: cannot unlock pxs_mutex\n");
      return paxos_protocol::ERR;
  }
  return paxos_protocol::OK;
}

paxos_protocol::status
acceptor::acceptreq(std::string src, paxos_protocol::acceptarg a, int &r)
{
  printf("acceptor::acceptreq: received accept from %s\n", src.c_str());

  // handle an acceptreq message from proposer
  if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("acceptor::acceptreq: cannot lock pxs_mutex\n");
      return paxos_protocol::ERR;
  }
  if (a.n >= n_h) {
    // accept the accept
    printf("acceptor::acceptreq: accept the accept\n");
    r = 1;
    n_a = a.n;
    v_a = a.v;
    printf("acceptor::acceptreq: n_a and v_a are updated, wrtite to the log\n");
    printf("acceptor::acceptreq: n_a.n -> %u, n_a.m -> %s\n", n_a.n, n_a.m.c_str());
    printf("acceptor::acceptreq: v_a -> %s\n", v_a.c_str());
    l->logprop(n_a, v_a);
  } else {
    // reject the prepare
    printf("acceptor::acceptreq: reject the accept\n");
    r = 0;
  }
  if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("acceptor::acceptreq: cannot unlock pxs_mutex\n");
      return paxos_protocol::ERR;
  }
  return paxos_protocol::OK;
}

paxos_protocol::status
acceptor::decidereq(std::string src, paxos_protocol::decidearg a, int &r)
{
  printf("acceptor::decidereq: received decide from %s\n", src.c_str());

  // handle an decide message from proposer
  if (pthread_mutex_lock(&pxs_mutex) != 0) {
      printf("acceptor::decidereq: cannot lock pxs_mutex\n");
      return paxos_protocol::ERR;
  }

  r = a.instance > instance_h ? 1 : 0;
  commit_wo(a.instance, a.v);

  if (pthread_mutex_unlock(&pxs_mutex) != 0) {
      printf("acceptor::decidereq: cannot unlock pxs_mutex\n");
      return paxos_protocol::ERR;
  }
  return paxos_protocol::OK;
}

void
acceptor::commit_wo(unsigned instance, std::string value)
{
  //assume pxs_mutex is held
  printf("acceptor::commit: instance=%d has v= %s\n", instance, value.c_str());
  if (instance > instance_h) {
    printf("commit: highestaccepteinstance = %d\n", instance);
    values[instance] = value;
    l->loginstance(instance, value);
    instance_h = instance;
    n_h.n = 0;
    n_h.m = me;
    n_a.n = 0;
    n_a.m = me;
    v_a.clear();
    if (cfg) {
      pthread_mutex_unlock(&pxs_mutex);
      cfg->paxos_commit(instance, value);
      pthread_mutex_lock(&pxs_mutex);
    }
  }
}

void
acceptor::commit(unsigned instance, std::string value)
{
  pthread_mutex_lock(&pxs_mutex);
  commit_wo(instance, value);
  pthread_mutex_unlock(&pxs_mutex);
}

std::string
acceptor::dump()
{
  return l->dump();
}

void
acceptor::restore(std::string s)
{
  l->restore(s);
  l->logread();
}



// For testing purposes

// Call this from your code between phases prepare and accept of proposer
void
proposer::breakpoint1()
{
  if (break1) {
    printf("Dying at breakpoint 1!\n");
    exit(1);
  }
}

// Call this from your code between phases accept and decide of proposer
void
proposer::breakpoint2()
{
  if (break2) {
    printf("Dying at breakpoint 2!\n");
    exit(1);
  }
}

void
proposer::breakpoint(int b)
{
  if (b == 3) {
    printf("Proposer: breakpoint 1\n");
    break1 = true;
  } else if (b == 4) {
    printf("Proposer: breakpoint 2\n");
    break2 = true;
  }
}
