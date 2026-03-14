/**
 * Async Plan Runner
 * 
 * Provides the "start and poll" MCP tools for Claude to launch
 * local execution stacks without hitting HTTP timeouts.
 */

import { spawn } from 'child_process';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, readFileSync } from 'fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const activeJobs = new Map();

/**
 * Launch a plan in the background.
 */
export function launchPlan(planPath) {
  const jobId = `job_${Date.now()}_${Math.floor(Math.random() * 1000)}`;
  
  if (!existsSync(planPath)) {
    return { error: `Plan file not found: ${planPath}` };
  }

  // Spawn the console TUI as a detached background process
  const child = spawn('node', [join(__dirname, 'console', 'run.js'), '--plan', planPath], {
    detached: true,
    stdio: 'ignore' // We don't want to capture stdout here, it's meant for the terminal
  });

  child.unref();

  activeJobs.set(jobId, {
    planPath,
    startTime: new Date().toISOString(),
    status: 'running',
    reportPath: planPath.replace('.json', '_report.json')
  });

  return {
    job_id: jobId,
    status: 'started',
    message: 'Plan execution launched in background. Use check_job_status to poll for results.',
    report_file: planPath.replace('.json', '_report.json')
  };
}

/**
 * Check the status of a background job.
 */
export function checkJobStatus(jobId) {
  const job = activeJobs.get(jobId);
  if (!job) {
    return { error: `Job not found: ${jobId}` };
  }

  // Check if the report file exists (written by run.js when finished)
  if (existsSync(job.reportPath)) {
    try {
      const report = JSON.parse(readFileSync(job.reportPath, 'utf-8'));
      job.status = report.status === 'completed' ? 'success' : 'failed';
      activeJobs.set(jobId, job);
      
      return {
        job_id: jobId,
        status: job.status,
        completed: true,
        report: report
      };
    } catch (e) {
      return {
        job_id: jobId,
        status: 'running',
        completed: false,
        message: 'Report file found but not fully written yet.'
      };
    }
  }

  return {
    job_id: jobId,
    status: 'running',
    completed: false,
    message: 'Execution in progress...'
  };
}
