#include "TaskManager.h"
#include <algorithm>
#include <ArduinoJson.h>

TaskManager::TaskManager() : lastUpdate_(0) {
}

TaskManager::~TaskManager() {
    tasks_.clear();
}

bool TaskManager::initialize() {
    tasks_.clear();
    lastUpdate_ = millis();
    return true;
}

void TaskManager::update() {
    uint32_t now = millis();
    
    // Sort tasks by priority (higher priority first)
    std::sort(tasks_.begin(), tasks_.end(), 
        [](const Task& a, const Task& b) {
            return static_cast<int>(a.priority) > static_cast<int>(b.priority);
        });
    
    // Execute due tasks
    for (auto& task : tasks_) {
        if (!task.enabled) {
            continue;
        }
        
        if (now - task.lastExecution >= task.interval) {
            executeTask(task);
        }
    }
    
    // Remove completed one-shot tasks
    removeCompletedTasks();
    
    lastUpdate_ = now;
}

size_t TaskManager::addTask(const String& name, std::function<void()> callback, 
                           uint32_t interval, TaskPriority priority, bool oneShot) {
    tasks_.emplace_back(name, callback, interval, priority, oneShot);
    return tasks_.size() - 1; // Return task index as ID
}

bool TaskManager::removeTask(size_t taskId) {
    if (taskId >= tasks_.size()) {
        return false;
    }
    
    tasks_.erase(tasks_.begin() + taskId);
    return true;
}

bool TaskManager::setTaskEnabled(size_t taskId, bool enabled) {
    if (taskId >= tasks_.size()) {
        return false;
    }
    
    tasks_[taskId].enabled = enabled;
    return true;
}

bool TaskManager::setTaskInterval(size_t taskId, uint32_t newInterval) {
    if (taskId >= tasks_.size()) {
        return false;
    }
    
    tasks_[taskId].interval = newInterval;
    return true;
}

String TaskManager::getTaskStats() const {
    DynamicJsonDocument doc(2048);
    JsonArray tasksArray = doc["tasks"].to<JsonArray>();
    
    for (size_t i = 0; i < tasks_.size(); i++) {
        const Task& task = tasks_[i];
        JsonObject taskObj = tasksArray.createNestedObject();
        
        taskObj["id"] = i;
        taskObj["name"] = task.name;
        taskObj["enabled"] = task.enabled;
        taskObj["interval"] = task.interval;
        taskObj["priority"] = static_cast<int>(task.priority);
        taskObj["one_shot"] = task.oneShot;
        taskObj["last_execution"] = task.lastExecution;
        
        uint32_t now = millis();
        uint32_t nextExecution = task.lastExecution + task.interval;
        taskObj["next_execution"] = (nextExecution > now) ? (nextExecution - now) : 0;
    }
    
    doc["total_tasks"] = tasks_.size();
    doc["last_update"] = lastUpdate_;
    
    String result;
    serializeJson(doc, result);
    return result;
}

void TaskManager::resetTimers() {
    uint32_t now = millis();
    for (auto& task : tasks_) {
        task.lastExecution = now;
    }
    lastUpdate_ = now;
}

void TaskManager::pauseAll() {
    for (auto& task : tasks_) {
        task.enabled = false;
    }
}

void TaskManager::resumeAll() {
    for (auto& task : tasks_) {
        task.enabled = true;
    }
    // Reset timers to prevent immediate execution after resume
    resetTimers();
}

void TaskManager::executeTask(Task& task) {
    uint32_t startTime = millis();
    
    try {
        task.callback();
        task.lastExecution = startTime;
        
        // Mark one-shot tasks as disabled after execution
        if (task.oneShot) {
            task.enabled = false;
        }
    } catch (...) {
        // Handle any exceptions that might occur during task execution
        // In a production system, you might want to log this error
        task.lastExecution = startTime;
    }
}

void TaskManager::removeCompletedTasks() {
    // Remove disabled one-shot tasks
    tasks_.erase(
        std::remove_if(tasks_.begin(), tasks_.end(),
            [](const Task& task) {
                return task.oneShot && !task.enabled;
            }),
        tasks_.end()
    );
}
