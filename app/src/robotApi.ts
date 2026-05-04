export type RobotAction =
  | "task_created"
  | "task_started"
  | "task_completed"
  | "focus_started"
  | "focus_finished"
  | "note_created"
  | "reminder_alert"
  | "idle";

const ROBOT_IP_KEY = "tabbie_robot_ip";

export function getRobotIp(): string {
  return localStorage.getItem(ROBOT_IP_KEY) || "192.168.1.121";
}

export function setRobotIp(ip: string) {
  localStorage.setItem(ROBOT_IP_KEY, ip);
}

export async function sendRobotAction(action: RobotAction, message?: string) {
  const ip = getRobotIp();

  try {
    const response = await fetch(`http://${ip}/api/robot/action`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        action,
        message: message || "",
        source: "tabbie-web",
        timestamp: Date.now(),
      }),
    });

    if (!response.ok) {
      throw new Error(`Robot error: ${response.status}`);
    }

    return true;
  } catch (error) {
    console.warn("Failed to send robot action:", error);
    return false;
  }
}
