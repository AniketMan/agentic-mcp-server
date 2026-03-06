/**
 * ue-bridge.ts
 * HTTP client that communicates with the ManusMCP C++ plugin running inside UE5.
 * All requests are sent to localhost:<port>/api/<endpoint>.
 *
 * The bridge handles:
 *   - Connection health checks with retry
 *   - GET requests with query parameters
 *   - POST requests with JSON bodies
 *   - Error wrapping for consistent MCP error reporting
 */

const DEFAULT_PORT = 9847;
const DEFAULT_TIMEOUT_MS = 30000;

export class UEBridge {
  private baseUrl: string;
  private timeoutMs: number;

  constructor(port: number = DEFAULT_PORT, timeoutMs: number = DEFAULT_TIMEOUT_MS) {
    this.baseUrl = `http://127.0.0.1:${port}`;
    this.timeoutMs = timeoutMs;
  }

  /**
   * Check if the UE5 server is running and responsive.
   * Returns the health response or throws if unreachable.
   */
  async health(): Promise<Record<string, unknown>> {
    return this.get("health");
  }

  /**
   * Send a GET request to the UE5 server.
   * @param endpoint - API endpoint name (without /api/ prefix)
   * @param params   - Query parameters as key-value pairs
   */
  async get(endpoint: string, params?: Record<string, string>): Promise<Record<string, unknown>> {
    let url = `${this.baseUrl}/api/${endpoint}`;
    if (params) {
      const searchParams = new URLSearchParams(params);
      url += `?${searchParams.toString()}`;
    }

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);

    try {
      const response = await fetch(url, {
        method: "GET",
        signal: controller.signal,
      });

      const text = await response.text();
      try {
        return JSON.parse(text);
      } catch {
        return { rawResponse: text };
      }
    } catch (error: unknown) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error(`Request to ${endpoint} timed out after ${this.timeoutMs}ms`);
      }
      throw new Error(
        `Failed to connect to ManusMCP server at ${this.baseUrl}. ` +
        `Is the UE5 editor running with the ManusMCP plugin enabled? ` +
        `Error: ${error instanceof Error ? error.message : String(error)}`
      );
    } finally {
      clearTimeout(timeout);
    }
  }

  /**
   * Send a POST request to the UE5 server.
   * @param endpoint - API endpoint name (without /api/ prefix)
   * @param body     - JSON body to send
   */
  async post(endpoint: string, body: Record<string, unknown> = {}): Promise<Record<string, unknown>> {
    const url = `${this.baseUrl}/api/${endpoint}`;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);

    try {
      const response = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
        signal: controller.signal,
      });

      const text = await response.text();
      try {
        return JSON.parse(text);
      } catch {
        return { rawResponse: text };
      }
    } catch (error: unknown) {
      if (error instanceof Error && error.name === "AbortError") {
        throw new Error(`Request to ${endpoint} timed out after ${this.timeoutMs}ms`);
      }
      throw new Error(
        `Failed to connect to ManusMCP server at ${this.baseUrl}. ` +
        `Is the UE5 editor running with the ManusMCP plugin enabled? ` +
        `Error: ${error instanceof Error ? error.message : String(error)}`
      );
    } finally {
      clearTimeout(timeout);
    }
  }
}
