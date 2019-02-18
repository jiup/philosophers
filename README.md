# Assignment 2: The Drinking Philosophers

| Course No. | Name          | Email                   |
| ---------- | ------------- | ----------------------- |
| CSC458     | Jiupeng Zhang | jzh149@ur.rochester.edu |



## Usage

Compile: `/path/to/jzh149/make`

Run by default: `/path/to/jzh149/philosophers`

Parameters:

- `-s`	session count *(default: 20)*

- `-f`	configuration file *(default: use the 5-phil model)*

- `-`	read graph from the standard input



## Implementation

**1)	Data structure**

```c++
std::vector<DiningState> dining_states;
std::vector<DrinkingState> drinking_states;
std::vector<std::vector<std::pair<int, Resource*>>> graph;
```

The relationship between philosophers is represented as an adjacent list, in my implementation, it is called `graph` in my program, the first and second dimension indicates the belongingness of an edge between two philosophers. Besides, the edges are abstractions of resources (in Chandy and Misra's paper, they are forks and bottles). Each philosopher has an identifier, with an identifier, we can query the status of a philosopher by access the `dining_states` and `drinking_states` vectors.

```c++
// line 28
struct Fork {
    std::mutex lock;
    std::condition_variable reqf_cond;
    std::condition_variable fork_cond;
    volatile bool hold;
    volatile bool reqf;
    volatile bool dirty = true;
};

struct Bottle {
    std::mutex lock;
    std::condition_variable condition{};
    volatile bool hold;
    volatile bool reqb;
    // volatile bool need{}; // ignored for simplicity
};

struct Resource {
    Fork fork;
    Bottle bottle;
};
```

The code above is my designed *resource* entity, which contains a fork and a bottle. Each edge between philosophers holds a resource so that the Chandy and Misra's solution can be corresponded by this model. Besides, each `Fork` and `Bottle` holds a mutex lock and a reqf_cond variable, the former one is used to protect the access on flags for an edge, and the latter is helping in thread switching and resource sharing (details will be discussed in the following paragraphs). Notice that the boolean variable `need` is ignored in my implementation, given the simplified scenario that *all adjacent bottles are needed* in each drinking session.



**2)	Core algorithm (Part I: Dining)**

- **Thinking**

  ```c++
  // line 297
  case DiningState::THINKING:
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.fork.lock);
          if (resource.fork.hold && resource.fork.dirty && resource.fork.reqf) {
              resource.fork.dirty = false;
              send_fork(id, ref_pair.first);
              resource.fork.hold = false;
          }
          pthread_mutex_unlock(&resource.fork.lock);
      }
  
      // (D1) A thinking, thirsty philosopher becomes hungry
      if (drinking_states[id] == DrinkingState::THIRSTY) {
          dining_states[id] = DiningState::HUNGRY;
      }
      break;
  ```

  In the *C&M's paper*, the `R2` command shows that a philosopher is responsible to release dirty forks if s/he is not eating, hence during thinking, a philosopher first tries to release forks if all required conditions are satisfied. After that, s/he update its status to *hungry* when the upper-level drinking philosopher has the status of *hungry* (correspond to the `D1` transition logic).

- **Hungry**

  ```c++
  // line 320
  case DiningState::HUNGRY:
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.fork.lock);
          // fork exists, yield precedence if it is dirty
          if (resource.fork.hold && resource.fork.dirty && resource.fork.reqf) {
              resource.fork.dirty = false;
              send_fork(id, ref_pair.first);
              resource.fork.hold = false;
          }
          if (!resource.fork.hold) {
              while (!resource.fork.reqf) {
                  // waiting for fork-ticket
                  pthread_cond_wait(&resource.fork.reqf_cond, &resource.fork.lock);
              }
              // single request sent
              send_reqf(id, ref_pair.first);
              resource.fork.reqf = false;
          }
          pthread_mutex_unlock(&resource.fork.lock);
      }
  
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.fork.lock);
          if (!resource.fork.hold && !resource.fork.reqf) {
              // waiting for fork
              pthread_cond_wait(&resource.fork.reqf_cond, &resource.fork.lock);
          }
          pthread_mutex_unlock(&resource.fork.lock);
      }
  
      // all forks received
      dining_states[id] = DiningState::EATING;
      break;
  ```

  The first `send_fork` *if* behavior the same as it in the thinking status, then, it checks existence of the `reqf` (shows the permission of requesting a fork), if a `reqf` not exists, a philosopher will wait until its partner release a `reqf` and notify her/him.

  After `reqf` received, a philosopher can send the request token/ticket to its partner so that they can send forks to her/him to eat. When all forks received, that philosopher can now start to eat.

- **Eating**

  ```c++
  // line 354
  case DiningState::EATING:
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.fork.lock);
          resource.fork.dirty = true; // already ate
          pthread_mutex_unlock(&resource.fork.lock);
      }
      // (D2) An eating, nonthirsty philosopher starts thinking
      if (drinking_states[id] != DrinkingState::THIRSTY) {
          dining_states[id] = DiningState::THINKING;
      }
      break;
  ```

  When a philosopher is eating, s/he will no longer listen to others' fork requests. After that, s/he set the fork to *dirty* and then waiting for the `D2` transition.

- **Messaging**

  ```c++
  // (R1) Requesting a fork f:
  void send_reqf(long from, long to) {
      auto it = std::find_if([from](std::pair<int, Resource*> ref_pair) -> bool {
          return from == ref_pair.first;
      });
      // (R3) Receiving a request token for f:
      it->second->fork.reqf = true;
      it->second->fork.reqf_cond.notify_one();
  }
  
  // (R2) Releasing a fork f:
  void send_fork(long from, long to) {
      auto it = std::find_if([from](std::pair<int, Resource*> ref_pair) -> bool {
          return from == ref_pair.first;
      });
      // (R4) Receiving a fork f:
      it->second->fork.dirty = false;
      it->second->fork.hold = true;
      it->second->fork.fork_cond.notify_one();
  }
  ```

  When sending a `reqf`, I set my current *reqf* to *false* and my neighbor's to *true*, then wake up my neighbor to continue her/his work.

  When sending a *fork*, I set my current *fork* to *false*, then *clean* it and notify after giving it to my partner, so that they can start eating.



**3)	Core algorithm (Part II: Drinking)**

- **Tranquil**

  ```c++
  // line 242
  case DrinkingState::TRANQUIL:
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.bottle.lock);
          if (resource.bottle.hold && resource.bottle.reqb && !resource.fork.hold) {
              send_bottle(id, ref_pair.first);
              resource.bottle.hold = false;
          }
          pthread_mutex_unlock(&resource.bottle.lock);
      }
      tranquil(id);
      drinking_states[id] = DrinkingState::THIRSTY;
      break;
  ```

  Similar to the *thinking philosopher*. a tranquil philosopher gives all used bottles if the others hold the fork (in the underlying *thinking philosopher*'s perspective). After that, a philisopher randomly sleep for a while and change its status to *thirsty*.

- **Thirsty**

  ```c++
  // line 256
  case DrinkingState::THIRSTY:
      // For simplicity and for ease of grading, each drinking session should employ
      // all adjacent bottles (not the arbitrary subset allowed by Chandy and Misra).
      for (std::pair<int, Resource> ref_pair : refs) {
          Resource resource = ref_pair.second;
          pthread_mutex_lock(&resource.bottle.lock);
          if (resource.bottle.hold && resource.bottle.reqb && !resource.fork.hold) {
              send_bottle(id, ref_pair.first);
              resource.bottle.hold = false;
          }
          if (!resource.bottle.hold) {
              while (!resource.bottle.reqb) {
                  // waiting for bottle-ticket
                  pthread_cond_wait(&resource.bottle.reqf_cond,
                                    &resource.bottle.lock);
              }
              // single request sent
              send_reqb(id, ref_pair.first);
              resource.bottle.reqb = false;
          }
          pthread_mutex_unlock(&resource.bottle.lock);
      }
      // all bottles received
      drinking_states[id] = DrinkingState::DRINKING;
      break;
  ```

  This part is almost the same as it in the dining solution. When `reqb` is required, a philosopher will wait on its reqf_cond until others' notify her/him.

- **Drinking**

  ```c++
  // line 288
  case DrinkingState::DRINKING:
      drinking(id);
      drinking_states[id] = DrinkingState::TRANQUIL;
      session++;
      if (session == session_cnt) {
          dining_states[id] = DiningState::THINKING;
      }
      break;
  ```

  A drinking philosopher no longer checks bottle requests until it finishes drinking. After that, it starts tranquil. Now, a session has successfully completed, so I update the session counter, repeat the whole process until it reaches the target.

- **Messaging**

  ```c++
  // (R1) Requesting a Bottle:
  void send_reqb(long from, long to) {
      auto it = std::find_if([from](std::pair<int, Resource*> ref_pair) -> bool {
          return from == ref_pair.first;
      });
      // (R3) Receive Request for a Bottle:
      it->second->bottle.reqb = true;
      it->second->bottle.condition.notify_one();
  }
  
  // (R2) Send a Bottle:
  void send_bottle(long from, long to) {
      auto it = std::find_if([from](std::pair<int, Resource*> ref_pair) -> bool {
          return from == ref_pair.first;
      });
      // (R4) Receive a Bottle:
      it->second->bottle.hold = true;
  }
  ```



## Test Result 

```shell
$ ./make
$ ./philosophers -s 100 -f configuration > output
$ python3 ./test_drinking.py configuration output

Okay, there are 5 philosophers
Everything looks good to me.
```

