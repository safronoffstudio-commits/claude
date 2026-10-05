import type { Config } from './config.js';

export interface EmailMessage {
  to: string;
  subject: string;
  text: string;
}

export interface Mailer {
  send(message: EmailMessage): Promise<void>;
}

/** Sends through Resend when RESEND_API_KEY is set; otherwise prints to the log (development). */
export function createMailer(config: Config['email']): Mailer {
  return {
    async send(message) {
      if (!config.resendApiKey) {
        console.info(`[email] (not sent — RESEND_API_KEY is not set)\nTo: ${message.to}\nSubject: ${message.subject}\n\n${message.text}\n`);
        return;
      }
      const response = await fetch('https://api.resend.com/emails', {
        method: 'POST',
        headers: { Authorization: `Bearer ${config.resendApiKey}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ from: config.from, to: [message.to], subject: message.subject, text: message.text }),
      });
      if (!response.ok) {
        throw new Error(`Resend responded ${response.status}: ${await response.text()}`);
      }
    },
  };
}
