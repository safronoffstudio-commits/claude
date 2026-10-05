import Anthropic from '@anthropic-ai/sdk';
import { z } from 'zod';
import type { Config } from '../config.js';
import { GenerationError, parseReelsOutput, type ReelsGenerator } from './generator.js';
import { buildUserMessage, SYSTEM_PROMPT } from './prompt.js';
import { ReelsOutputSchema } from './schema.js';

// Zod's own JSON Schema export already has the strict shape structured outputs needs
// (required + additionalProperties: false) and, unlike the SDK's zod helper, keeps `enum`
// as a grammar constraint instead of folding it into a description.
const { $schema: _ignored, ...OUTPUT_JSON_SCHEMA } = z.toJSONSchema(ReelsOutputSchema, { io: 'output' }) as Record<string, unknown>;

const MAX_TOKENS = 64_000;

export function createClaudeGenerator(config: Config['ai']): ReelsGenerator {
  const client = new Anthropic({ apiKey: config.apiKey, maxRetries: 4 });

  return {
    name: 'claude',
    async generate(req) {
      let message: Anthropic.Beta.BetaMessage;
      try {
        // Streaming keeps a long generation (big transcript + thinking) clear of HTTP timeouts.
        const stream = client.beta.messages.stream({
          model: config.model,
          max_tokens: MAX_TOKENS,
          // A safety-classifier decline is retried server-side on Anthropic's recommended model.
          betas: ['server-side-fallback-2026-07-01'],
          fallbacks: 'default',
          thinking: { type: 'adaptive' },
          output_config: {
            effort: config.effort,
            format: { type: 'json_schema', schema: OUTPUT_JSON_SCHEMA },
          },
          system: [{ type: 'text', text: SYSTEM_PROMPT, cache_control: { type: 'ephemeral' } }],
          messages: [{ role: 'user', content: buildUserMessage(req) }],
        });
        message = await stream.finalMessage();
      } catch (err) {
        throw toGenerationError(err);
      }

      if (message.stop_reason === 'refusal') {
        throw new GenerationError('refused', `Request declined (${message.stop_details?.category ?? 'no category'})`);
      }
      if (message.stop_reason === 'max_tokens') {
        throw new GenerationError('truncated', 'Output hit max_tokens');
      }

      // After a mid-stream fallback the JSON is split across text blocks: the declined
      // model's partial and the fallback model's continuation. Joined, they are one document.
      const text = message.content
        .filter((block): block is Anthropic.Beta.BetaTextBlock => block.type === 'text')
        .map((block) => block.text)
        .join('');

      return {
        output: parseReelsOutput(text, req.options.count),
        model: message.model,
        usage: {
          input: message.usage.input_tokens,
          output: message.usage.output_tokens,
          cacheRead: message.usage.cache_read_input_tokens ?? 0,
          cacheWrite: message.usage.cache_creation_input_tokens ?? 0,
        },
      };
    },
  };
}

function toGenerationError(err: unknown): GenerationError {
  if (err instanceof GenerationError) return err;
  const message = err instanceof Error ? err.message : String(err);
  if (err instanceof Anthropic.AuthenticationError || err instanceof Anthropic.PermissionDeniedError) {
    return new GenerationError('config', `Anthropic credentials rejected: ${message}`, { cause: err });
  }
  if (err instanceof Anthropic.BadRequestError || err instanceof Anthropic.NotFoundError) {
    return new GenerationError('bad_request', message, { cause: err });
  }
  if (err instanceof Anthropic.RateLimitError) {
    return new GenerationError('rate_limited', message, { cause: err });
  }
  if (err instanceof Anthropic.InternalServerError) {
    return new GenerationError('overloaded', message, { cause: err });
  }
  if (err instanceof Anthropic.APIConnectionError) {
    return new GenerationError('network', message, { cause: err });
  }
  return new GenerationError('unknown', message, { cause: err });
}
