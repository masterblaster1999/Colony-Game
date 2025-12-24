// ------------------------------ Event Bus ------------------------------
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    int subscribe(EventKind k, Handler h) {
        int id = ++sid_;
        subs_[k].emplace_back(id, std::move(h));
        return id;
    }
    void unsubscribeAll() { subs_.clear(); }
    void publish(const Event& e) {
        replay_.push_back({stamp_++, e});
        if(auto it=subs_.find(e.kind); it!=subs_.end())
            for (auto& [_,h]: it->second) h(e);
    }
    void clearReplay(){ replay_.clear(); }
    struct ReplayEntry{ uint64_t t; Event e; };
    const std::vector<ReplayEntry>& replay() const { return replay_; }
private:
    int sid_ = 0;
    uint64_t stamp_ = 0;
    std::unordered_map<EventKind, std::vector<std::pair<int,Handler>>> subs_;
    std::vector<ReplayEntry> replay_;
};

