#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include "../LeafNodeTypes.h"

/**
 * @brief Task structure for the task manager
 */
struct Task {
    String name;
    std::function<void()> callback;
    uint32_t interval;
    uint32_t lastExecution;
    TaskPriority priority;
    bool enabled;
    bool oneShot;

    Task(const String& taskName, std::function<void()> taskCallback, 
         uint32_t taskInterval, TaskPriority taskPriority = TaskPriority::NORMAL, 
         bool isOneShot = false)
        : name(taskName), callback(taskCallback), interval(taskInterval),
          lastExecution(0), priority(taskPriority), enabled(true), oneShot(isOneShot) {}
};

/**
 * @brief Cooperative task manager for handling multiple tasks
 * 
 * Provides a simple cooperative multitasking system for managing
 * periodic tasks without blocking operations.
 */
class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    /**
     * @brief Initialize the task manager
     * @return true if initialization was successful
     */
    bool initialize();

    /**
     * @brief Update task manager - execute due tasks
     * Should be called from the main loop
     */
    void update();

    /**
     * @brief Add a new task
     * @param name Task name (for debugging/monitoring)
     * @param callback Function to execute
     * @param interval Execution interval in milliseconds
     * @param priority Task priority
     * @param oneShot If true, task runs only once
     * @return Task ID for reference
     */
    size_t addTask(const String& name, std::function<void()> callback, 
                   uint32_t interval, TaskPriority priority = TaskPriority::NORMAL,
                   bool oneShot = false);

    /**
     * @brief Remove a task by ID
     * @param taskId Task ID to remove
     * @return true if task was removed
     */
    bool removeTask(size_t taskId);

    /**
     * @brief Enable/disable a task
     * @param taskId Task ID
     * @param enabled true to enable, false to disable
     * @return true if task was found and updated
     */
    bool setTaskEnabled(size_t taskId, bool enabled);

    /**
     * @brief Change task interval
     * @param taskId Task ID
     * @param newInterval New interval in milliseconds
     * @return true if task was found and updated
     */
    bool setTaskInterval(size_t taskId, uint32_t newInterval);

    /**
     * @brief Get number of active tasks
     * @return Number of tasks
     */
    size_t getTaskCount() const { return tasks_.size(); }

    /**
     * @brief Get task execution statistics
     * @return Statistics as JSON string
     */
    String getTaskStats() const;

    /**
     * @brief Reset all task timers (useful after sleep/wake)
     */
    void resetTimers();

    /**
     * @brief Pause all tasks (for OTA updates or system operations)
     */
    void pauseAll();

    /**
     * @brief Resume all tasks
     */
    void resumeAll();

private:
    std::vector<Task> tasks_;
    uint32_t lastUpdate_;
    
    void executeTask(Task& task);
    void removeCompletedTasks();
};
