// ------------------------------ Job Queue (with agent-aware selection) ------------------------------
class JobQueue {
public:
    void push(const Job& j, int priority=0){
        queue_.push(Entry{counter_++, {priority, seq_++}, j});
    }
    bool empty() const { return queue_.empty(); }
    size_t size() const { return queue_.size(); }

    // Pop best-scoring job for agent among top K entries.
    std::optional<Job> popBestFor(const Agent& agent, const Grid& grid, int minuteOfDay, int K=12){
        if(queue_.empty()) return std::nullopt;
        std::vector<Entry> tmp;
        for(int i=0; i<K && !queue_.empty(); ++i){ tmp.push_back(queue_.top()); queue_.pop(); }
        int bestIdx=-1; double bestScore=-1e18;
        for(size_t i=0;i<tmp.size();++i){
            const auto& e = tmp[i];
            double s = score(e, agent, grid, minuteOfDay);
            if(s>bestScore){ bestScore=s; bestIdx=(int)i; }
        }
        std::optional<Job> res;
        for(size_t i=0;i<tmp.size();++i){
            if((int)i==bestIdx){ res = tmp[i].job; }
            else queue_.push(tmp[i]);
        }
        return res;
    }

private:
    struct Entry {
        uint64_t id;
        JobPriority pri;
        Job job;
    };
    struct Cmp {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.pri.p != b.pri.p) return a.pri.p < b.pri.p; // max-heap
            return a.pri.createdOrder > b.pri.createdOrder;   // FIFO
        }
    };

    double score(const Entry& e, const Agent& a, const Grid& grid, int minuteOfDay) const {
        // base: priority
        double s = e.pri.p * 10.0;
        // closer is better
        int dist = a.pos.manhattan(e.job.target);
        s -= dist * 0.5;
        // skill bonus
        s += a.skills[e.job.kind] * 2.0;
        // schedule: slight penalty when not in Work block
        Schedule::Block b = a.schedule.blockAtMinute(minuteOfDay);
        if(b!=Schedule::Work) s -= 10.0;
        // needs: if food job and hungry, bump
        if((e.job.kind==JobKind::Cook || e.job.kind==JobKind::Farm) && a.hunger>60) s += 8.0;
        return s;
    }

    std::priority_queue<Entry, std::vector<Entry>, Cmp> queue_;
    uint64_t counter_ = 0;
    uint64_t seq_ = 0;
};

